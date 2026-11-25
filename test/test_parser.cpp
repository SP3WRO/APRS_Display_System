// Testy parsera APRS uruchamiane na PC:
//   g++ -std=c++17 -Itest/arduino_shim -I. test/test_parser.cpp aprs_parser.cpp -o test_parser && ./test_parser
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>
#include "aprs_parser.h"
#include "icons_aprs.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STR(a, b) do { checks++; String _a = (a); String _b = (b); if (_a != _b) { failures++; \
  printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a.c_str(), _b.c_str()); } } while (0)
#define CHECK_NEAR(a, b, eps) do { checks++; double _a = (a), _b = (b); if (std::fabs(_a - _b) > (eps)) { failures++; \
  printf("  FAIL %s:%d: %f != %f\n", __FILE__, __LINE__, _a, _b); } } while (0)

static const float MY_LAT = 52.1234f, MY_LON = 16.1234f;

static APRSPacket parseIS(const char *line) {
  APRSPacket pkt;
  bool ok = parseAPRSISLine(line, pkt);
  CHECK(ok);
  parseAPRSPacket(pkt, MY_LAT, MY_LON);
  return pkt;
}

static bool contains(const String &s, const char *what) { return s.indexOf(what) >= 0; }

static void addAddr(std::vector<uint8_t> &f, const char *call, int ssid, bool last, bool hbit = false) {
  for (int i = 0; i < 6; i++) {
    char c = call[i] ? call[i] : ' ';
    if (!call[i]) { for (; i < 6; i++) f.push_back(' ' << 1); break; }
    f.push_back(c << 1);
  }
  f.push_back(0x60 | (ssid << 1) | (last ? 1 : 0) | (hbit ? 0x80 : 0));
}

static void testUncompressedPosition() {
  puts("pozycja standardowa");
  APRSPacket p = parseIS("SP3WRO-9>APDR16,WIDE1-1,qAR,SR3NOW:!5207.40N/01607.40E>Test komentarza");
  CHECK_NEAR(p.lat, 52.1233, 0.0005);
  CHECK_NEAR(p.lon, 16.1233, 0.0005);
  CHECK(p.symbolTable == '/');
  CHECK(p.symbolCode == '>');
  CHECK_STR(p.comment, "Test komentarza");
  CHECK_STR(p.radioModel, "APRSdroid");
  CHECK(p.distance >= 0 && p.distance < 1);
  CHECK(!p.isEmergency);
}

static void testCourseSpeedAltitude() {
  puts("kurs/predkosc/wysokosc");
  APRSPacket p = parseIS("SP3ABC>APRS:=5210.00N/01620.00E>090/054/A=001000 jade");
  CHECK(contains(p.parsedText, "100km/h, 90st"));
  CHECK(contains(p.parsedText, "Wys: 305m"));
}

static void testBase91() {
  puts("Base91 (przyklad ze specyfikacji)");
  APRSPacket p = parseIS("TEST>APRS:!/5L!!<*e7>7P[komentarz");
  CHECK_NEAR(p.lat, 49.5, 0.001);
  CHECK_NEAR(p.lon, -72.75, 0.001);
  CHECK(p.symbolCode == '>');
  CHECK(contains(p.parsedText, "88st"));
  CHECK_STR(p.comment, "komentarz");
}

static void testShortPacketsDoNotHang() {
  puts("krotkie/uszkodzone ramki (dawniej zawieszaly urzadzenie)");
  const char *lines[] = {"AB1CD>APRS:@12", "AB1CD>APRS:/x", "AB1CD>APRS:;OBJ", "AB1CD>APRS:)IT!", "AB1CD>APRS:!", "AB1CD>APRS:`12"};
  for (const char *l : lines) {
    APRSPacket p = parseIS(l);
    CHECK(p.distance < 0);
  }
}

static void testMicE() {
  puts("Mic-E (Polska, TM-D710)");
  APRSPacket p;
  p.sourceCall = "SP3WRO-9";
  p.destCall = "URPW40";
  p.payload = String("`,#D\x1c\x1c\x1c>/]Hello=");
  parseAPRSPacket(p, MY_LAT, MY_LON);
  CHECK_NEAR(p.lat, 52.1233, 0.0005);
  CHECK_NEAR(p.lon, 16.1233, 0.0005);
  CHECK(p.symbolCode == '>');
  CHECK_STR(p.radioModel, "TM-D710");
  CHECK_STR(p.comment, "Hello");
  CHECK(contains(p.parsedText, "Off Duty"));
  CHECK(!p.isEmergency);

  puts("Mic-E (polkula zachodnia, dlugosc > 100)");
  APRSPacket w;
  w.destCall = "SSRUVT";
  w.payload = String("`(#f\x1c\x1c\x1c>/");
  parseAPRSPacket(w, MY_LAT, MY_LON);
  CHECK_NEAR(w.lat, 33.4273, 0.001);
  CHECK_NEAR(w.lon, -112.129, 0.001);

  puts("Mic-E emergency");
  APRSPacket e;
  e.destCall = "520W40";
  e.payload = String("`,#D\x1c\x1c\x1c>/");
  parseAPRSPacket(e, MY_LAT, MY_LON);
  CHECK(e.isEmergency);
}

static void testWeather() {
  puts("pogoda z pozycja");
  APRSPacket p = parseIS("SP3WX>APRS:!5207.40N/01607.40E_090/005g010t050r000p000P000h65b10132");
  CHECK(p.isWX);
  CHECK(contains(p.parsedText, "Temp: 10 C"));
  CHECK(contains(p.parsedText, "Wilgotność: 65 %"));
  CHECK(contains(p.parsedText, "Ciśn: 1013.2 hPa"));

  puts("pogoda bez pozycji");
  APRSPacket q = parseIS("SP3WX>APRS:_10090556c220s004g005t077r000p000P000h50b09900");
  CHECK(q.isWX);
  CHECK(contains(q.parsedText, "Temp: 25 C"));
  CHECK(contains(q.parsedText, "Ciśn: 990.0 hPa"));
}

static void testMessages() {
  puts("wiadomosci");
  APRSPacket m = parseIS("SQ3ABC>APDR16,TCPIP*::SP3WRO   :Czesc, slychac mnie?{42");
  CHECK(m.isMessage);
  CHECK(!m.isAck);
  CHECK_STR(m.msgAddressee, "SP3WRO");
  CHECK_STR(m.msgText, "Czesc, slychac mnie?");
  CHECK_STR(m.msgId, "42");

  APRSPacket r = parseIS("SQ3ABC>APDR16,TCPIP*::SP3WRO   :Odpowiedz{AB}CD");
  CHECK_STR(r.msgId, "AB");
  CHECK_STR(r.msgText, "Odpowiedz");

  APRSPacket a = parseIS("SQ3ABC>APDR16,TCPIP*::SP3WRO-9 :ack42");
  CHECK(a.isMessage);
  CHECK(a.isAck);
  CHECK_STR(a.msgAddressee, "SP3WRO-9");
  CHECK_STR(a.msgId, "42");

  APRSPacket j = parseIS("SQ3ABC>APDR16::SP3WRO   :rej7");
  CHECK(j.isReject);
  CHECK_STR(j.msgId, "7");

  CHECK_STR(padCall9("sp3wro"), "SP3WRO   ");
}

static void testObjectAndThirdParty() {
  puts("obiekt i ruch third-party");
  APRSPacket o = parseIS("SR3NOW>APRS:;LEADER   *092345z4903.50N/07201.75W>088/036");
  CHECK_STR(o.sourceCall, "LEADER");
  CHECK_NEAR(o.lat, 49.0583, 0.001);
  CHECK_NEAR(o.lon, -72.0292, 0.001);

  APRSPacket t = parseIS("SR3NOW>APRS,TCPIP*:}SP3XYZ>APK008,TCPIP,SR3NOW*:!5207.40N/01607.40E[ test");
  CHECK_STR(t.sourceCall, "SP3XYZ");
  CHECK_STR(t.radioModel, "TH-D74");
  CHECK(t.symbolCode == '[');
}

static bool iconEmpty(const uint8_t *bmp) {
  for (int i = 0; i < 128; i++) if (bmp[i]) return false;
  return true;
}

static void testSymbols() {
  puts("symbole APRS");
  // Przesunięcie tablicy (np. przez komentarz z backslashem na końcu linii) zostawia
  // ostatnią ikonę ("~") pustą i przesuwa resztę - pilnujemy obu tabel
  CHECK(!iconEmpty(aprs_symbols_primary['~' - '!']));
  CHECK(!iconEmpty(aprs_symbols_alternate['~' - '!']));
  // Sąsiednie, różne symbole nie mogą mieć tej samej grafiki
  const char pairs[][2] = {{'_', '`'}, {'v', 'w'}, {'U', 'V'}, {'\\', ']'}, {'[', '\\'}};
  for (const auto &pr : pairs) {
    CHECK(memcmp(aprs_symbols_primary[pr[0] - '!'], aprs_symbols_primary[pr[1] - '!'], 128) != 0);
  }
  // Logo firm pominięte celowo
  CHECK(iconEmpty(aprs_symbols_primary['M' - '!']));
  CHECK(iconEmpty(aprs_symbols_alternate['K' - '!']));

  // Symbol z ramek: WX, bus/van, nakładka w formacie skompresowanym (a-j = 0-9)
  APRSPacket wx = parseIS("SP3WRO-13>APRS:!5207.40N/01607.40E_090/005g010t050h65b10132");
  CHECK(wx.symbolTable == '/' && wx.symbolCode == '_');
  APRSPacket van = parseIS("SQ3RCH>APRS:!5207.40N/01607.40Ev");
  CHECK(van.symbolTable == '/' && van.symbolCode == 'v');
  APRSPacket ov = parseIS("TEST>APRS:!a5L!!<*e7#7P[");
  CHECK(ov.symbolTable == '0' && ov.symbolCode == '#');
  APRSPacket lora = parseIS("SR3L>APLRG1:!L5L!!<*e7&  G");
  CHECK(lora.symbolTable == 'L' && lora.symbolCode == '&');
}

static void testHeardDirect() {
  puts("odbior bezposredni (DX)");
  CHECK(heardDirect(parseIS("SP3A>APRS,WIDE1-1,WIDE2-1:!5207.40N/01607.40E-x")));
  CHECK(!heardDirect(parseIS("SP3A>APRS,SR3DPN*,WIDE2-1:!5207.40N/01607.40E-x")));
  CHECK(!heardDirect(parseIS("SP3A>APRS,SR3DPN,WIDE2*:!5207.40N/01607.40E-x")));
  CHECK(heardDirect(parseIS("SP3A>APRS,WIDE1-1,qAR,SR3IG:!5207.40N/01607.40E-x")));
  CHECK(!heardDirect(parseIS("SP3A>APRS,SR3DPN*,qAR,SR3IG:!5207.40N/01607.40E-x")));
  CHECK(!heardDirect(parseIS("SR3IG>APRS:}SP3A>APRS,TCPIP,SR3IG*:!5207.40N/01607.40E-x")));

  APRSPacket o = parseIS("SR3DPN>APMI06:;145.650-P*111111z5222.28N/01654.49Er145.650MHz");
  CHECK(o.isObject);
  CHECK(heardDirect(o));
}

static void testEmergencyKeyword() {
  puts("slowo EMERGENCY");
  APRSPacket n = parseIS("SP3A>APRS:!5207.40N/01607.40E-Emergency net co wtorek");
  CHECK(!n.isEmergency);
  APRSPacket e = parseIS("SP3A>APRS:!5207.40N/01607.40E-EMERGENCY potrzebna pomoc");
  CHECK(e.isEmergency);
}

static void testAX25() {
  puts("ramka AX.25 (KISS)");
  std::vector<uint8_t> f;
  addAddr(f, "APDR16", 0, false);
  addAddr(f, "SP3WRO", 9, false);
  addAddr(f, "SR3NOW", 0, false, true);
  addAddr(f, "WIDE2", 1, true);
  f.push_back(0x03);
  f.push_back(0xF0);
  const char *info = ">Pozdrawiam z \xc5\x81odzi";   // "Łodzi" w UTF-8
  for (const char *c = info; *c; c++) f.push_back((uint8_t)*c);

  APRSPacket p;
  CHECK(parseAX25Frame(f.data(), f.size(), p));
  CHECK_STR(p.sourceCall, "SP3WRO-9");
  CHECK_STR(p.destCall, "APDR16");
  CHECK_STR(p.path, "SR3NOW*,WIDE2-1");
  parseAPRSPacket(p, MY_LAT, MY_LON);
  CHECK_STR(p.comment, "Pozdrawiam z Łodzi");

  std::vector<uint8_t> broken(10, 0x40);
  APRSPacket b;
  CHECK(!parseAX25Frame(broken.data(), broken.size(), b));
}

static void testUtils() {
  puts("narzedzia");
  CHECK(aprsPasscode("N0CALL") == 13023);
  CHECK(aprsPasscode("sp3wro-9") == aprsPasscode("SP3WRO"));
  CHECK_STR(formatAprsLat(52.123f), "5207.38N");
  CHECK_STR(formatAprsLat(52.99999f), "5300.00N");
  CHECK_STR(formatAprsLon(-16.5f), "01630.00W");
  CHECK_STR(toDisplayAscii("Żółć \xc2\xb0" "C"), "Zolc C");
  CHECK_STR(toDisplayText("Żółć 20\xc2\xb0" "C"), "Żółć 20\xc2\xb0" "C");
  CHECK_STR(toDisplayText("ab\x1c\x7f" "c\xc5" "d\xff"), "abcd");   // sterujące i uszkodzone UTF-8 wypadają
  CHECK_STR(identifyRadio("APDW17"), "Direwolf");
  CHECK_STR(identifyRadio("APLRT1"), "LoRa Tracker");
  CHECK_STR(getCompassDir(350), "N");
  CHECK_STR(getCompassDir(135), "SE");
  String dao = "komentarz !W5!";
  cleanDAO(dao);
  CHECK_STR(dao, "komentarz");
  CHECK(decodePHG("PHG5132").indexOf("25W") >= 0);
}

int main() {
  testUncompressedPosition();
  testCourseSpeedAltitude();
  testBase91();
  testShortPacketsDoNotHang();
  testMicE();
  testWeather();
  testMessages();
  testObjectAndThirdParty();
  testSymbols();
  testHeardDirect();
  testEmergencyKeyword();
  testAX25();
  testUtils();
  printf("\n%d sprawdzen, %d bledow\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
