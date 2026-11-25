#include "aprs_parser.h"
#include <math.h>

// --- IDENTYFIKACJA RADIA (TOCALL) ---
struct TocallEntry { const char *prefix; const char *name; };

// Kolejność ma znaczenie: dłuższe/dokładniejsze prefiksy przed ogólnymi.
static const TocallEntry TOCALLS[] = {
  {"APY008", "VX-8R"},   {"APY01D", "FTM-100D"}, {"APY02D", "FTM-200D"},
  {"APY03D", "FTM-300D"}, {"APY04D", "FTM-400D"}, {"APY05D", "FTM-500D"},
  {"APY1DR", "FT1D"},    {"APY2DR", "FT2D"},     {"APY3DR", "FT3D"},
  {"APY5DR", "FT5D"},

  {"APK003", "TH-D7"},   {"APK004", "TM-D700"},  {"APK005", "TM-D710"},
  {"APK006", "TH-D72"},  {"APK007", "TM-D710G"}, {"APK008", "TH-D74"},
  {"APK009", "TH-D75"},

  {"APA878", "AT-D878UV"}, {"APA578", "AT-D578UV"},

  {"APLRG", "LoRa iGate"}, {"APLRT", "LoRa Tracker"},
  {"APLR", "LoRa Tracker"}, {"APLORA", "LoRa Tracker"},

  {"APDR", "APRSdroid"},
  {"APDW", "Direwolf"},
  {"APRX", "aprx"},
  {"APWW", "APRSIS32"},
  {"APU25", "UI-View"},
  {"APOT", "OpenTracker"},
  {"APMI", "MicroSat"},
  {"APBM", "BrandMeister"},
  {"APZESP", "APRS Display"},
  {"APX", "Xastir"},

  {"APL", "Alinco"},
  {"APOX", "PicoAPRS"},
};

String identifyRadio(const String &tocall) {
  for (const TocallEntry &e : TOCALLS) {
    if (tocall.startsWith(e.prefix)) return e.name;
  }
  return "";
}

String identifyMicERadio(String &comment) {
  while (comment.length() > 0) {
    char c = comment.charAt(comment.length() - 1);
    if (c < 32 || c > 126) comment = comment.substring(0, comment.length() - 1);
    else break;
  }

  if (comment.length() == 0) return "";

  String model = "";

  if (comment.length() >= 2) {
    String last2 = comment.substring(comment.length() - 2);

    if (last2 == " X") model = "AP510";
    else if (last2 == "(5") model = "D578UV";
    else if (last2 == "(8") model = "D878UV";
    else if (last2 == "*9") model = "AVRT9";
    else if (last2 == "*v") model = "Tracker (KissOZ)";
    else if (last2 == ":2") model = "VP-Tracker";
    else if (last2 == "[1") model = "APRSdroid";
    else if (last2 == "^v") model = "anyfrog";
    else if (last2 == "_ ") model = "VX-8";
    else if (last2 == "_\"") model = "FTM-350";
    else if (last2 == "_#") model = "VX-8G";
    else if (last2 == "_$") model = "FT1D";
    else if (last2 == "_%") model = "FTM-400DR";
    else if (last2 == "_(") model = "FT2D";
    else if (last2 == "_)") model = "FTM-100D";
    else if (last2 == "_0") model = "FT3D";
    else if (last2 == "_1") model = "FTM-300D";
    else if (last2 == "_2") model = "FTM-200D";
    else if (last2 == "_3") model = "FT5D";
    else if (last2 == "_4") model = "FTM-500D";
    else if (last2 == "_5") model = "FTM-510D";
    else if (last2 == "_6") model = "FTX-1";
    else if (last2 == "_7") model = "FTM-310D";
    else if (last2 == "|3") model = "TinyTrak3";
    else if (last2 == "|4") model = "TinyTrak4";

    if (model != "") {
      comment = comment.substring(0, comment.length() - 2);
      return model;
    }
  }

  char firstChar = comment.charAt(0);
  char lastChar = comment.charAt(comment.length() - 1);

  if (firstChar == ']') {
    if (lastChar == '=') {
      model = "TM-D710";
      comment = comment.substring(0, comment.length() - 1);
    } else {
      model = "TM-D700";
    }
    return model;
  } else if (firstChar == '>') {
    if (lastChar == '=') {
      model = "TH-D72";
      comment = comment.substring(0, comment.length() - 1);
    } else if (lastChar == '^') {
      model = "TH-D74";
      comment = comment.substring(0, comment.length() - 1);
    } else if (lastChar == '&') {
      model = "TH-D75";
      comment = comment.substring(0, comment.length() - 1);
    } else {
      model = "TH-D7A";
    }
    return model;
  }

  if (comment.length() >= 2 && comment.charAt(comment.length() - 2) == '_') {
    comment = comment.substring(0, comment.length() - 2);
    return "Yaesu";
  }

  return "";
}

// --- NARZĘDZIA TEKSTOWE ---
String removePolishChars(String input) {
  String out = input;
  out.replace("ą", "a"); out.replace("Ą", "A");
  out.replace("ć", "c"); out.replace("Ć", "C");
  out.replace("ę", "e"); out.replace("Ę", "E");
  out.replace("ł", "l"); out.replace("Ł", "L");
  out.replace("ń", "n"); out.replace("Ń", "N");
  out.replace("ó", "o"); out.replace("Ó", "O");
  out.replace("ś", "s"); out.replace("Ś", "S");
  out.replace("ź", "z"); out.replace("Ź", "Z");
  out.replace("ż", "z"); out.replace("Ż", "Z");
  return out;
}

// Tekst do wyświetlenia: drukowalne ASCII + poprawne sekwencje UTF-8 (polskie litery,
// stopnie itp.). Znaki sterujące i uszkodzone bajty (np. binarne pola Mic-E) wypadają.
String toDisplayText(const String &input) {
  String out = "";
  unsigned int len = input.length();
  for (unsigned int i = 0; i < len; i++) {
    uint8_t c = (uint8_t)input.charAt(i);
    if (c < 0x80) {
      if ((c >= 32 && c <= 126) || c == '\n') out += (char)c;
      continue;
    }
    int extra = (c >= 0xC2 && c <= 0xDF) ? 1 : (c >= 0xE0 && c <= 0xEF) ? 2 : (c >= 0xF0 && c <= 0xF4) ? 3 : -1;
    if (extra < 0 || i + extra >= len) continue;
    bool valid = true;
    for (int k = 1; k <= extra; k++) {
      if (((uint8_t)input.charAt(i + k) & 0xC0) != 0x80) { valid = false; break; }
    }
    if (!valid) continue;
    for (int k = 0; k <= extra; k++) out += input.charAt(i + k);
    i += extra;
  }
  return out;
}

// Tylko ASCII (po transliteracji PL) - do nadawania, bo APRS w eterze to ASCII.
String toDisplayAscii(const String &input) {
  String in = removePolishChars(input);
  String out = "";
  for (unsigned int i = 0; i < in.length(); i++) {
    char c = in.charAt(i);
    if ((c >= 32 && c <= 126) || c == '\n') out += c;
  }
  return out;
}

void cleanDAO(String &commentText) {
  int daoW = commentText.indexOf("!W");
  if (daoW > 0 && daoW + 4 <= (int)commentText.length() && commentText.charAt(daoW + 3) == '!') {
    commentText = commentText.substring(0, daoW);
  }
  int daow = commentText.indexOf("!w");
  if (daow > 0 && daow + 4 <= (int)commentText.length() && commentText.charAt(daow + 3) == '!') {
    commentText = commentText.substring(0, daow);
  }
  commentText.trim();
}

String padCall9(String call) {
  call.toUpperCase();
  if (call.length() > 9) call = call.substring(0, 9);
  while (call.length() < 9) call += " ";
  return call;
}

int aprsPasscode(String call) {
  call.toUpperCase();
  call.trim();
  int dash = call.indexOf('-');
  if (dash >= 0) call = call.substring(0, dash);
  unsigned int hash = 0x73e2;
  for (unsigned int i = 0; i < call.length(); i += 2) {
    hash ^= ((unsigned char)call.charAt(i)) << 8;
    if (i + 1 < call.length()) hash ^= (unsigned char)call.charAt(i + 1);
  }
  return hash & 0x7fff;
}

static String formatAprsCoord(float value, int degDigits, char pos, char neg) {
  long hundredths = lround(fabs(value) * 6000.0);   // setne części minuty
  long deg = hundredths / 6000;
  long minHundredths = hundredths % 6000;
  char buf[16];
  snprintf(buf, sizeof(buf), degDigits == 2 ? "%02ld%02ld.%02ld%c" : "%03ld%02ld.%02ld%c",
           deg, minHundredths / 100, minHundredths % 100, value >= 0 ? pos : neg);
  return String(buf);
}

String formatAprsLat(float lat) { return formatAprsCoord(lat, 2, 'N', 'S'); }
String formatAprsLon(float lon) { return formatAprsCoord(lon, 3, 'E', 'W'); }

// --- MATEMATYKA I KIERUNKI ---
float calculateDistance(float lat1, float lon1, float lat2, float lon2) {
  float R = 6371.0;
  float dLat = (lat2 - lat1) * M_PI / 180.0;
  float dLon = (lon2 - lon1) * M_PI / 180.0;
  lat1 = lat1 * M_PI / 180.0;
  lat2 = lat2 * M_PI / 180.0;
  float a = sin(dLat / 2) * sin(dLat / 2) + sin(dLon / 2) * sin(dLon / 2) * cos(lat1) * cos(lat2);
  float c = 2 * atan2(sqrt(a), sqrt(1 - a));
  return R * c;
}

float calculateBearing(float lat1, float lon1, float lat2, float lon2) {
  float dLon = (lon2 - lon1) * M_PI / 180.0;
  lat1 = lat1 * M_PI / 180.0;
  lat2 = lat2 * M_PI / 180.0;
  float y = sin(dLon) * cos(lat2);
  float x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dLon);
  float brng = atan2(y, x) * 180.0 / M_PI;
  if (brng < 0) brng += 360.0;
  return brng;
}

String getCompassDir(float bearing) {
  if (bearing < 0) return "";
  const char *dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW", "N"};
  int idx = round(bearing / 45.0);
  if (idx < 0 || idx > 8) return "";
  return dirs[idx];
}

float parseCoord(const String &coordStr, bool isLat) {
  if (coordStr.length() < 7) return 0.0;
  float degrees = coordStr.substring(0, isLat ? 2 : 3).toFloat();
  float minutes = coordStr.substring(isLat ? 2 : 3, isLat ? 7 : 8).toFloat();
  float decimalDeg = degrees + (minutes / 60.0);
  char dir = coordStr.charAt(coordStr.length() - 1);
  if (dir == 'S' || dir == 'W') decimalDeg = -decimalDeg;
  return decimalDeg;
}

// --- DEKODERY ---
bool isNumericStr(const String &str) {
  for (unsigned int i = 0; i < str.length(); i++) {
    if (!isDigit(str.charAt(i)) && str.charAt(i) != ' ' && str.charAt(i) != '-') return false;
  }
  return true;
}

String decodeWeather(String &commentText) {
  String out = "";
  bool hasData = false;

  int spaceIdx = commentText.indexOf(' ');
  String wxData = (spaceIdx >= 0) ? commentText.substring(0, spaceIdx) : commentText;
  String restComment = (spaceIdx >= 0) ? commentText.substring(spaceIdx + 1) : "";
  int wxLen = wxData.length();

  String dirStr = "", spdStr = "";

  if (wxLen >= 7 && wxData.charAt(3) == '/') {
    dirStr = wxData.substring(0, 3);
    spdStr = wxData.substring(4, 7);
  } else if (wxData.indexOf('c') >= 0 && wxData.indexOf('s') >= 0) {
    int cIdx = wxData.indexOf('c');
    int sIdx = wxData.indexOf('s');
    if (cIdx + 3 < wxLen && sIdx + 3 < wxLen) {
      dirStr = wxData.substring(cIdx + 1, cIdx + 4);
      spdStr = wxData.substring(sIdx + 1, sIdx + 4);
    }
  } else {
    int uscore = wxData.indexOf('_');
    if (uscore >= 0 && uscore + 8 <= wxLen && wxData.charAt(uscore + 4) == '/') {
      dirStr = wxData.substring(uscore + 1, uscore + 4);
      spdStr = wxData.substring(uscore + 5, uscore + 8);
    }
  }

  auto extractWx = [&](char tag, int len) -> String {
    int idx = wxData.indexOf(tag);
    if (idx >= 0 && idx + len < wxLen) {
      String val = wxData.substring(idx + 1, idx + 1 + len);
      if (isNumericStr(val)) return val;
    }
    return "";
  };

  String gStr = extractWx('g', 3);

  bool hasDir = (dirStr != "" && isNumericStr(dirStr));
  bool hasGust = (gStr != "");
  bool hasSpd = (spdStr != "" && isNumericStr(spdStr));

  if (hasGust || hasSpd || hasDir) {
    String windLine = "";

    if (hasGust) {
      float gustKmh = gStr.toFloat() * 1.609;
      windLine += "Wmax: " + String(gustKmh, 1) + "km/h";
    } else if (hasSpd) {
      float spdKmh = spdStr.toFloat() * 1.609;
      windLine += "Wiatr: " + String(spdKmh, 1) + "km/h";
    }

    if (hasDir) {
      if (windLine.length() > 0) windLine += " ";
      windLine += dirStr + "st";
    }

    if (windLine.length() > 0) {
      out += windLine + "\n";
      hasData = true;
    }
  }

  String tStr = extractWx('t', 3);
  if (tStr != "") {
    int tempF = tStr.toInt();
    int tempC = round((tempF - 32.0) * 5.0 / 9.0);
    out += "Temp: " + String(tempC) + " C\n";
    hasData = true;
  }

  int hIdx = wxData.indexOf('h');
  if (hIdx >= 0 && hIdx + 3 <= wxLen) {
    String hStr = wxData.substring(hIdx + 1, hIdx + 3);
    if (isNumericStr(hStr)) {
      int hum = hStr.toInt();
      if (hum == 0) hum = 100;
      out += "Wilgotność: " + String(hum) + " %\n";
      hasData = true;
    }
  }

  String bStr = extractWx('b', 5);
  if (bStr != "") {
    float press = bStr.toFloat() / 10.0;
    out += "Ciśn: " + String(press, 1) + " hPa\n";
    hasData = true;
  }

  String rStr = extractWx('r', 3);
  if (rStr != "") {
    out += "Opad(1h): " + String(rStr.toFloat() * 0.254, 1) + " mm\n";
    hasData = true;
  }

  String pStr = extractWx('p', 3);
  if (pStr != "") {
    out += "Opad(24h): " + String(pStr.toFloat() * 0.254, 1) + " mm\n";
    hasData = true;
  }

  String PStr = extractWx('P', 3);
  if (PStr != "") {
    out += "Opad(od 0:00): " + String(PStr.toFloat() * 0.254, 1) + " mm\n";
    hasData = true;
  }

  if (!hasData) return "";

  out.trim();
  commentText = restComment;
  return out;
}

String decodePHG(const String &payload) {
  int phgIdx = payload.indexOf("PHG");
  if (phgIdx >= 0 && phgIdx + 7 <= (int)payload.length()) {
    String phgData = payload.substring(phgIdx + 3, phgIdx + 7);
    for (int i = 0; i < 4; i++) {
      if (!isDigit(phgData.charAt(i))) return "";
    }
    int p = phgData.charAt(0) - '0';
    int h = phgData.charAt(1) - '0';
    int g = phgData.charAt(2) - '0';
    int d = phgData.charAt(3) - '0';

    int power = p * p;
    int heightM = round(10.0 * pow(2, h) * 0.3048);
    String dir = (d == 0) ? "Omni" : String(d * 45) + "st";

    return "[Radio] " + String(power) + "W, Ant: " + String(heightM) + "m, " + String(g) + "dB, " + dir;
  }
  return "";
}

bool decodeMicE(const String &dest, const String &info, float &lat, float &lon, char &symTable, char &symCode, int &speed, int &course, String &statusMsg) {
  String dest6 = dest;
  int dashIdx = dest6.indexOf('-');
  if (dashIdx > 0) dest6 = dest6.substring(0, dashIdx);
  while (dest6.length() < 6) dest6 += " ";

  if (info.length() < 9) return false;

  int a = 0, b = 0, c = 0;
  bool isCustom = false;
  auto checkMsg = [&](char ch, int &bit) {
    if (ch >= 'A' && ch <= 'K') { bit = 1; isCustom = true; }
    else if (ch >= 'P' && ch <= 'Z') { bit = 1; }
    else { bit = 0; }
  };
  checkMsg(dest6.charAt(0), a);
  checkMsg(dest6.charAt(1), b);
  checkMsg(dest6.charAt(2), c);

  int msgId = (a << 2) | (b << 1) | c;
  if (msgId == 0) statusMsg = "Emergency!";
  else if (msgId == 1) statusMsg = isCustom ? "Custom-6" : "Priority";
  else if (msgId == 2) statusMsg = isCustom ? "Custom-5" : "Special";
  else if (msgId == 3) statusMsg = isCustom ? "Custom-4" : "Committed";
  else if (msgId == 4) statusMsg = isCustom ? "Custom-3" : "Returning";
  else if (msgId == 5) statusMsg = isCustom ? "Custom-2" : "In Service";
  else if (msgId == 6) statusMsg = isCustom ? "Custom-1" : "En Route";
  else if (msgId == 7) statusMsg = isCustom ? "Custom-0" : "Off Duty";

  int dLat[6];
  int s = 0, w = 0, latOff = 0;

  for (int i = 0; i < 6; i++) {
    char ch = dest6.charAt(i);
    if (ch >= '0' && ch <= '9') dLat[i] = ch - '0';
    else if (ch >= 'A' && ch <= 'J') dLat[i] = ch - 'A';
    else if (ch >= 'P' && ch <= 'Y') dLat[i] = ch - 'P';
    else if (ch == 'K' || ch == 'L' || ch == 'Z') dLat[i] = 0;
    else return false;
  }

  char c3 = dest6.charAt(3);
  if (!(c3 >= 'P' && c3 <= 'Z')) s = 1;

  // Bajt 5 = przesunięcie długości (+100), bajt 6 = W/E (APRS101, rozdz. 10)
  char c4 = dest6.charAt(4);
  if (c4 >= 'P' && c4 <= 'Z') latOff = 100;

  char c5 = dest6.charAt(5);
  if (c5 >= 'P' && c5 <= 'Z') w = 1;

  lat = (dLat[0] * 10 + dLat[1]) + (dLat[2] * 10 + dLat[3]) / 60.0 + (dLat[4] * 10 + dLat[5]) / 6000.0;
  if (s) lat = -lat;

  int dLon = (unsigned char)info.charAt(1) - 28;
  if (latOff) dLon += 100;
  if (dLon >= 180 && dLon <= 189) dLon -= 80;
  else if (dLon >= 190 && dLon <= 199) dLon -= 190;

  int mLon = (unsigned char)info.charAt(2) - 28;
  if (mLon >= 60) mLon -= 60;

  int hLon = (unsigned char)info.charAt(3) - 28;
  lon = dLon + mLon / 60.0 + hLon / 6000.0;
  if (w) lon = -lon;

  speed = 0; course = 0;
  int spdByte = (unsigned char)info.charAt(4) - 28;
  int crsByte1 = (unsigned char)info.charAt(5) - 28;
  int crsByte2 = (unsigned char)info.charAt(6) - 28;
  if (spdByte >= 0 && crsByte1 >= 0 && crsByte2 >= 0) {
    speed = (spdByte * 10) + (crsByte1 / 10);
    if (speed >= 800) speed -= 800;
    course = ((crsByte1 % 10) * 100) + crsByte2;
    if (course >= 400) course -= 400;
  }

  symCode = info.charAt(7);
  symTable = info.charAt(8);

  return true;
}

// Pozycja skompresowana zaczyna się od znaku tablicy symboli na pozycji `start`.
bool decodeBase91(const String &payload, int start, float &lat, float &lon, char &symTable, char &symCode, int &commentOffset, String &extra) {
  int len = payload.length();
  extra = "";
  if (start < 0 || start + 10 > len) return false;

  // W formacie skompresowanym tablica to '/', '\' lub nakładka A-Z / a-j (bez cyfr).
  char table = payload.charAt(start);
  bool validTable = (table == '/' || table == '\\' || (table >= 'A' && table <= 'Z') || (table >= 'a' && table <= 'j'));
  if (!validTable) return false;

  for (int j = start + 1; j < start + 9; j++) {
    char b = payload.charAt(j);
    if (b < 33 || b > 124) return false;
  }

  // W formacie skompresowanym nakładki cyfrowe 0-9 są kodowane literami a-j
  symTable = (table >= 'a' && table <= 'j') ? (char)('0' + (table - 'a')) : table;
  symCode = payload.charAt(start + 9);

  long y = 0;
  y += (long)(payload.charAt(start + 1) - 33) * 753571;
  y += (long)(payload.charAt(start + 2) - 33) * 8281;
  y += (long)(payload.charAt(start + 3) - 33) * 91;
  y += (long)(payload.charAt(start + 4) - 33);
  lat = 90.0 - ((float)y / 380926.0);

  long x = 0;
  x += (long)(payload.charAt(start + 5) - 33) * 753571;
  x += (long)(payload.charAt(start + 6) - 33) * 8281;
  x += (long)(payload.charAt(start + 7) - 33) * 91;
  x += (long)(payload.charAt(start + 8) - 33);
  lon = -180.0 + ((float)x / 190463.0);

  if (lat < -90 || lat > 90 || lon < -180 || lon > 180) return false;

  // Bajty cs + T: kurs/prędkość albo wysokość
  if (start + 13 <= len) {
    char cByte = payload.charAt(start + 10);
    char sByte = payload.charAt(start + 11);
    char tByte = payload.charAt(start + 12);
    if (cByte != ' ' && cByte >= 33 && sByte >= 33) {
      int t = tByte - 33;
      if (t >= 0 && (t & 0x18) == 0x10) {
        int altFeet = round(pow(1.002, (cByte - 33) * 91 + (sByte - 33)));
        extra = "Wys: " + String((int)round(altFeet * 0.3048)) + "m";
      } else if (cByte >= '!' && cByte <= 'z' && cByte != '{') {
        int course = (cByte - 33) * 4;
        int speedKmh = round((pow(1.08, sByte - 33) - 1.0) * 1.852);
        if (speedKmh > 0 || course > 0) extra = String(speedKmh) + "km/h, " + String(course) + "st";
      }
    }
    commentOffset = start + 13;
  } else {
    commentOffset = len;
  }
  return true;
}

// --- RAMKI ---
static String extractCallsign(const uint8_t *buffer, int startOffset) {
  String call = "";
  for (int i = 0; i < 6; i++) {
    char c = buffer[startOffset + i] >> 1;
    if (c != ' ') call += c;
  }
  uint8_t ssid = (buffer[startOffset + 6] >> 1) & 0x0F;
  if (ssid > 0) call += "-" + String(ssid);
  return call;
}

bool parseAX25Frame(const uint8_t *buf, int len, APRSPacket &pkt) {
  if (len < 16) return false;

  // Szukamy końca pola adresowego (bit rozszerzenia), maks. 2 + 8 digipeaterów
  int addrEnd = -1;
  for (int off = 0; off + 7 <= len && off < 70; off += 7) {
    if (buf[off + 6] & 0x01) { addrEnd = off + 7; break; }
  }
  if (addrEnd < 14 || addrEnd + 2 > len) return false;

  pkt.destCall = extractCallsign(buf, 0);
  pkt.sourceCall = extractCallsign(buf, 7);

  int lastUsed = -1;
  for (int off = 14; off < addrEnd; off += 7) {
    if (buf[off + 6] & 0x80) lastUsed = off;   // bit H - przez tego digi ramka już przeszła
  }
  pkt.path = "";
  for (int off = 14; off < addrEnd; off += 7) {
    if (pkt.path.length() > 0) pkt.path += ",";
    pkt.path += extractCallsign(buf, off);
    if (off == lastUsed) pkt.path += "*";
  }

  // Pomijamy pola Control (0x03) i PID (0xF0). Bajty >= 0x80 zostają (UTF-8, Mic-E).
  pkt.payload = "";
  for (int i = addrEnd + 2; i < len; i++) {
    uint8_t c = buf[i];
    if (c >= 28 || c == '\n' || c == '\r') pkt.payload += (char)c;
  }

  pkt.rawPacket = pkt.sourceCall + ">" + pkt.destCall;
  if (pkt.path.length() > 0) pkt.rawPacket += "," + pkt.path;
  pkt.rawPacket += ":" + pkt.payload;
  return true;
}

bool parseAPRSISLine(const String &line, APRSPacket &pkt) {
  if (line.startsWith("#") || line.length() < 10) return false;
  int pos1 = line.indexOf('>');
  int pos2 = line.indexOf(':');
  if (pos1 <= 0 || pos2 <= pos1) return false;

  pkt.rawPacket = line;
  pkt.sourceCall = line.substring(0, pos1);
  int commaPos = line.indexOf(',', pos1);
  if (commaPos > 0 && commaPos < pos2) {
    pkt.destCall = line.substring(pos1 + 1, commaPos);
    pkt.path = line.substring(commaPos + 1, pos2);
  } else {
    pkt.destCall = line.substring(pos1 + 1, pos2);
    pkt.path = "";
  }
  pkt.payload = line.substring(pos2 + 1);
  return true;
}

bool heardDirect(const APRSPacket &pkt) {
  if (pkt.isThirdParty) return false;
  String path = pkt.path;
  int q = path.indexOf("qA");
  if (q >= 0) path = path.substring(0, q);
  return path.indexOf('*') < 0;
}

// --- GŁÓWNY DEKODER ---
static void setPosition(APRSPacket &pkt, float lat, float lon, float myLat, float myLon) {
  pkt.lat = lat;
  pkt.lon = lon;
  pkt.distance = calculateDistance(myLat, myLon, lat, lon);
  pkt.bearing = calculateBearing(myLat, myLon, lat, lon);
}

static void parseMessage(APRSPacket &pkt) {
  int colonIdx = pkt.payload.indexOf(':', 1);
  if (colonIdx <= 1 || colonIdx > 10) return;

  pkt.isMessage = true;
  pkt.msgAddressee = pkt.payload.substring(1, colonIdx);
  pkt.msgAddressee.trim();
  pkt.msgAddressee.toUpperCase();

  String body = pkt.payload.substring(colonIdx + 1);
  String lower = body;
  lower.toLowerCase();

  // ack/rej: "ack123" lub nowy format "ack12}AB"
  if ((lower.startsWith("ack") || lower.startsWith("rej")) && body.length() <= 12 && body.indexOf(' ') < 0) {
    pkt.isAck = lower.startsWith("ack");
    pkt.isReject = !pkt.isAck;
    String id = body.substring(3);
    int close = id.indexOf('}');
    if (close >= 0) id = id.substring(0, close);
    id.trim();
    pkt.msgId = id;
    pkt.msgText = body;
  } else {
    int brace = body.lastIndexOf('{');
    if (brace >= 0) {
      String id = body.substring(brace + 1);
      int close = id.indexOf('}');
      if (close >= 0) id = id.substring(0, close);
      id.trim();
      if (id.length() > 5) id = id.substring(0, 5);
      pkt.msgId = id;
      body = body.substring(0, brace);
    }
    pkt.msgText = body;
  }

  pkt.parsedText = "[SMS] " + pkt.msgText;
  pkt.comment = pkt.parsedText;
}

void parseAPRSPacket(APRSPacket &pkt, float myLat, float myLon) {
  pkt.symbolTable = '/';
  pkt.symbolCode = 'L';
  pkt.distance = -1;
  pkt.bearing = -1;
  pkt.lat = 0.0;
  pkt.lon = 0.0;
  pkt.parsedText = pkt.payload;
  pkt.comment = pkt.payload;
  pkt.isEmergency = false;
  pkt.isWX = false;
  pkt.isMessage = false;
  pkt.isAck = false;
  pkt.isReject = false;
  pkt.isObject = false;
  pkt.isThirdParty = false;

  pkt.radioModel = identifyRadio(pkt.destCall);

  if (pkt.payload.length() == 0) return;

  char dti = pkt.payload.charAt(0);

  // Ruch third-party (np. z iGate): }ZNAK>DEST,PATH:payload
  if (dti == '}') {
    pkt.isThirdParty = true;
    int innerColon = pkt.payload.indexOf(':');
    if (innerColon > 0) {
      int innerGreater = pkt.payload.indexOf('>');
      if (innerGreater > 1 && innerGreater < innerColon) {
        pkt.sourceCall = pkt.payload.substring(1, innerGreater);
        int innerComma = pkt.payload.indexOf(',', innerGreater);
        int destEnd = (innerComma > 0 && innerComma < innerColon) ? innerComma : innerColon;
        pkt.destCall = pkt.payload.substring(innerGreater + 1, destEnd);
        pkt.radioModel = identifyRadio(pkt.destCall);
      }
      pkt.payload = pkt.payload.substring(innerColon + 1);
      pkt.comment = pkt.payload;
      pkt.parsedText = pkt.payload;
      if (pkt.payload.length() == 0) return;
      dti = pkt.payload.charAt(0);
    }
  }

  if (dti == ':') {
    parseMessage(pkt);
    pkt.msgText = toDisplayText(pkt.msgText);
    pkt.comment = toDisplayText(pkt.comment);
    pkt.parsedText = toDisplayText(pkt.parsedText);
    return;
  }

  // Słowo EMERGENCY (wielkimi literami, jak w specyfikacji) - nie "emergency net" w komentarzu
  if (pkt.payload.indexOf("EMERGENCY") >= 0) pkt.isEmergency = true;

  if (dti == '>') {
    pkt.comment = pkt.payload.substring(1);
    pkt.parsedText = "Status: " + pkt.comment;
  }

  bool isPositional = (dti == '!' || dti == '=' || dti == '/' || dti == '@' || dti == ';' || dti == ')');
  bool isStandard = false;
  bool isBase91 = false;
  int nameEnd = -1;   // koniec nazwy obiektu/itemu

  if (dti == ';' || dti == ')') {
    pkt.isObject = true;
    for (int i = 1; i <= 10 && i < (int)pkt.payload.length(); i++) {
      char c = pkt.payload.charAt(i);
      if ((dti == ';' && i == 10 && (c == '*' || c == '_')) || (dti == ')' && (c == '!' || c == '_'))) { nameEnd = i; break; }
    }
    if (dti == ';' && nameEnd < 0) {
      // obiekty spoza specyfikacji - szukamy pierwszego znacznika
      for (int i = 1; i <= 10 && i < (int)pkt.payload.length(); i++) {
        char c = pkt.payload.charAt(i);
        if (c == '*' || c == '_') { nameEnd = i; break; }
      }
    }
    if (nameEnd > 1) {
      String objName = pkt.payload.substring(1, nameEnd);
      objName.trim();
      pkt.sourceCall = objName;
    }
  }

  if (isPositional) {
    int dataStart = -1;
    int plen = pkt.payload.length();
    for (int i = 7; i < plen - 11; i++) {
      char latDir = pkt.payload.charAt(i);
      if ((latDir == 'N' || latDir == 'S') && pkt.payload.charAt(i - 3) == '.') {
        char lonDir = pkt.payload.charAt(i + 10);
        if ((lonDir == 'E' || lonDir == 'W') && pkt.payload.charAt(i + 7) == '.') {
          dataStart = i - 7;
          break;
        }
      }
    }

    if (dataStart >= 0) {
      pkt.symbolTable = pkt.payload.charAt(dataStart + 8);
      pkt.symbolCode = pkt.payload.charAt(dataStart + 18);

      float pLat = parseCoord(pkt.payload.substring(dataStart, dataStart + 8), true);
      float pLon = parseCoord(pkt.payload.substring(dataStart + 9, dataStart + 18), false);

      if (pLat != 0.0 && pLon != 0.0) {
        setPosition(pkt, pLat, pLon, myLat, myLon);
        isStandard = true;

        if (plen > dataStart + 19) pkt.comment = pkt.payload.substring(dataStart + 19);
        else pkt.comment = "";

        String spdCrsStr = "";
        if (plen >= dataStart + 26 && pkt.payload.charAt(dataStart + 22) == '/') {
          String cseStr = pkt.payload.substring(dataStart + 19, dataStart + 22);
          String spdStr = pkt.payload.substring(dataStart + 23, dataStart + 26);
          if (isNumericStr(cseStr) && isNumericStr(spdStr)) {
            int cse = cseStr.toInt();
            int spdKmh = round(spdStr.toInt() * 1.852);
            spdCrsStr = String(spdKmh) + "km/h, " + String(cse) + "st";
          }
        }

        String altStr = "";
        int aIdx = pkt.payload.indexOf("/A=");
        if (aIdx > 0 && aIdx + 9 <= plen) {
          long altFeet = pkt.payload.substring(aIdx + 3, aIdx + 9).toInt();
          int altM = round(altFeet * 0.3048);
          altStr = "Wys: " + String(altM) + "m";
        }

        String telemetry = "";
        if (spdCrsStr.length() > 0 || altStr.length() > 0) {
          telemetry = "[";
          if (spdCrsStr.length() > 0) telemetry += spdCrsStr;
          if (spdCrsStr.length() > 0 && altStr.length() > 0) telemetry += " | ";
          if (altStr.length() > 0) telemetry += altStr;
          telemetry += "]\n";
        }

        cleanDAO(pkt.comment);
        pkt.parsedText = "Poz (" + String(pLat, 4) + ", " + String(pLon, 4) + ")\n" + telemetry + pkt.comment;
      }
    }

    if (!isStandard) {
      int compStart = -1;
      if (dti == '!' || dti == '=') compStart = 1;
      else if (dti == '/' || dti == '@') compStart = 8;
      else if (dti == ';') compStart = 18;
      else if (dti == ')' && nameEnd > 0) compStart = nameEnd + 1;

      float bLat = 0, bLon = 0;
      char bSymT, bSymC;
      int cOffset = 0;
      String extra;

      if (decodeBase91(pkt.payload, compStart, bLat, bLon, bSymT, bSymC, cOffset, extra)) {
        setPosition(pkt, bLat, bLon, myLat, myLon);
        pkt.symbolTable = bSymT;
        pkt.symbolCode = bSymC;
        isBase91 = true;

        if ((int)pkt.payload.length() > cOffset) pkt.comment = pkt.payload.substring(cOffset);
        else pkt.comment = "";

        cleanDAO(pkt.comment);
        pkt.parsedText = "Base91 (" + String(bLat, 4) + ", " + String(bLon, 4) + ")\n";
        if (extra.length() > 0) pkt.parsedText += "[" + extra + "]\n";
        pkt.parsedText += pkt.comment;
      }
    }
  }

  if (!isStandard && !isBase91 && pkt.payload.length() >= 9 && (dti == '`' || dti == '\'' || dti == 0x1C || dti == 0x1D)) {
    float mLat = 0, mLon = 0;
    char mSymT, mSymC;
    int mSpeed = 0, mCourse = 0;
    String mStatus = "";

    if (decodeMicE(pkt.destCall, pkt.payload, mLat, mLon, mSymT, mSymC, mSpeed, mCourse, mStatus)) {
      if (mStatus == "Emergency!") pkt.isEmergency = true;

      setPosition(pkt, mLat, mLon, myLat, myLon);
      pkt.symbolTable = mSymT;
      pkt.symbolCode = mSymC;
      // Mic-E używa pola DEST do pozycji, więc TOCALL nie identyfikuje radia
      pkt.radioModel = "";

      String micEComment = "";
      if (pkt.payload.length() > 9) micEComment = pkt.payload.substring(9);

      String micERadio = identifyMicERadio(micEComment);
      if (micERadio.length() > 0) pkt.radioModel = micERadio;

      micEComment.trim();

      String extraInfo = "";

      int brac = -1;
      for (int i = 0; i < 6 && i < (int)micEComment.length(); i++) {
        if (micEComment.charAt(i) == '}') { brac = i; break; }
      }

      if (brac >= 3) {
        int a = micEComment.charAt(brac - 3) - 33;
        int b = micEComment.charAt(brac - 2) - 33;
        int c = micEComment.charAt(brac - 1) - 33;
        if (a >= 0 && a <= 90 && b >= 0 && b <= 90 && c >= 0 && c <= 90) {
          int altitude = (a * 8281) + (b * 91) + c - 10000;
          extraInfo += "Alt: " + String(altitude) + "m ";
        }
      }

      if (brac >= 0) {
        micEComment = micEComment.substring(brac + 1);
      } else {
        if (micEComment.length() > 0) {
          char tByte = micEComment.charAt(0);
          if (tByte == '>' || tByte == ']' || tByte == '`' || tByte == '\'' || tByte == '_') micEComment = micEComment.substring(1);
        }
        if (micEComment.length() >= 5 && (micEComment.charAt(0) == '`' || micEComment.charAt(0) == '\'')) micEComment = micEComment.substring(5);
      }

      if (micEComment.length() >= 10 && micEComment.substring(7, 10) == "MHz") {
        extraInfo += "Freq: " + micEComment.substring(0, 10) + " ";
        micEComment = micEComment.substring(10);
      }

      cleanDAO(micEComment);

      String telemetryData = "[" + mStatus;
      if (mSpeed > 0 || mCourse > 0) {
        int speedKmh = round(mSpeed * 1.852);
        telemetryData += " | " + String(speedKmh) + "km/h, " + String(mCourse) + "st";
      }
      telemetryData += "] " + extraInfo;

      pkt.parsedText = "Mic-E (" + String(mLat, 4) + ", " + String(mLon, 4) + ")\n" + telemetryData + micEComment;
      pkt.comment = micEComment;
    }
  }

  // Raport pogodowy bez pozycji: _MMDDHHMMc...s...g...t...
  if (dti == '_' && pkt.payload.length() > 9) {
    String wx = pkt.payload.substring(9);
    String wxStr = decodeWeather(wx);
    if (wxStr.length() > 0) {
      pkt.symbolTable = '/';
      pkt.symbolCode = '_';
      pkt.isWX = true;
      pkt.parsedText = wxStr;
      pkt.comment = wxStr;
      pkt.comment.replace('\n', ' ');
    }
  }

  if (!pkt.isWX && (pkt.symbolTable == '/' || pkt.symbolTable == '\\' || isalnum(pkt.symbolTable)) && pkt.symbolCode == '_') {
    String restComment = pkt.comment;
    String wxStr = decodeWeather(restComment);

    if (wxStr.length() > 0) {
      pkt.isWX = true;
      pkt.parsedText = wxStr;
      if (restComment.length() > 0) pkt.parsedText += "\nOpis: " + restComment;

      pkt.comment = wxStr;
      if (restComment.length() > 0) {
        String cleanRest = restComment;
        cleanRest.replace('\n', ' ');
        pkt.comment += " | " + cleanRest;
      }
    }
  }

  if (pkt.payload.indexOf("PHG") >= 0) {
    String phgText = decodePHG(pkt.payload);
    if (phgText.length() > 0) pkt.parsedText += "\n" + phgText;
  }

  pkt.comment = toDisplayText(pkt.comment);
  pkt.comment.trim();
  pkt.parsedText = toDisplayText(pkt.parsedText);
}
