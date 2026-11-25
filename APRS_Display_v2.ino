// APRS Display System v2 - SP3WRO
// Pliki w tym folderze (Arduino IDE otwiera je jako zakładki):
//   APRS_Display_v2.ino - zmienne globalne, konfiguracja, setup(), loop()
//   display.ino         - rysowanie ekranu e-ink
//   network.ino         - KISS/UART/BT/TCP/APRS-IS, beacon, filtry, WiFi
//   messages.ino        - wiadomości APRS z ACK i ponawianiem
//   stats.ino           - statystyki i "DX dnia"
//   web.ino             - panel WWW, mapa, API JSON
//   aprs_parser.*       - dekoder ramek (testowany na PC, patrz test/)
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <WiFiClient.h>
#include <time.h>
#include "BluetoothSerial.h"
#include "app.h"
#include "icons.h"
#include "icons_aprs.h"   // pełny zestaw symboli APRS (tools/convert-icons.ps1)
#include <math.h>

// --- KONFIGURACJA EKRANU ---
#define ENABLE_GxEPD2_GFX 0
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>   // czcionki z polskimi literami (biblioteka U8g2_for_Adafruit_GFX)

#define EPD_CS 5
#define EPD_DC 17
#define EPD_RST 16
#define EPD_BUSY 4
GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(GxEPD2_420_GDEY042T81(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

// --- KONFIGURACJA UART ---
#define VP_RX_PIN 26
#define VP_TX_PIN 27
#define VP_RX_BUFFER 4096   // odświeżenie e-ink trwa do 4 s - przy 9600 bps to ok. 4 KB danych
HardwareSerial VPSerial(2);

// --- SERWER, KLIENT, BT ---
WebServer server(80);
Preferences prefs;
WiFiClient tcpClient;
BluetoothSerial SerialBT;

Config appConfig;

APRSPacket history[MAX_HISTORY];
APRSPacket lastPacket;

String terminalLines[MAX_TERM_LINES];
int termIndex = 0;

// --- BUFOR ANTYDUPLIKATOWY ---
struct SeenStation {
  String call;
  String payload;
  unsigned long lastSeen;
};
SeenStation seenStations[MAX_SEEN];
int seenIndex = 0;

// --- WIADOMOŚCI I STATYSTYKI ---
MessageEntry messages[MAX_MESSAGES];   // [0] = najnowsza
Outbox outbox;
int unreadMessages = 0;
int nextMsgId = 1;

HeardStation heard[MAX_HEARD];
unsigned long totalPackets = 0;
String dxCall = "";
float dxDistance = -1;
float dxBearing = -1;
String dxTime = "";   // godzina ustanowienia DX (gdy jest czas z NTP)
int dxDay = -1;

// --- FLAGI I STOPERY ---
bool forceFullRefresh = true;
unsigned long lastTcpRetry = 0;
unsigned long lastTcpRx = 0;
unsigned long lastRefreshTime = 0;
unsigned long lastBeaconTime = 0;
bool isisLoggedIn = false;
bool newDataToDisplay = false;
unsigned long lastDisplayUpdate = 0;
bool emergencyActive = false;
unsigned long emergencyStart = 0;
bool apActive = false;
bool timeConfigured = false;

// --- BT FALLBACK ---
bool btFallbackTriggered = false;
unsigned long btConnectAttemptTime = 0;
unsigned long lastBtRetry = 0;
int btFailedAttempts = 0;

// --- ZMIENNE PROTOKOŁU KISS ---
#define FEND 0xC0
#define FESC 0xDB
#define TFEND 0xDC
#define TFESC 0xDD
#define KISS_BUFFER_SIZE 512
uint8_t kissBuffer[KISS_BUFFER_SIZE];
int kissIndex = 0;
bool kissEscaped = false;
bool kissOverflow = false;

// --- TERMINAL LOGGER ---
void addToTerminal(String log) {
  terminalLines[termIndex] = log;
  termIndex = (termIndex + 1) % MAX_TERM_LINES;
  Serial.println(log);
}

String getTerminalText() {
  String txt = "";
  for (int i = 0; i < MAX_TERM_LINES; i++) {
    int idx = (termIndex + i) % MAX_TERM_LINES;
    if (terminalLines[idx].length() > 0) txt += terminalLines[idx] + "\n";
  }
  return txt;
}

bool isBtMode() {
  return appConfig.currentMode == MODE_KISS_BT && !btFallbackTriggered;
}

// Znak używany do logowania w APRS-IS
String effectiveIsCall() {
  String c = appConfig.isCall;
  c.trim();
  if (c.length() == 0) c = appConfig.callsign;
  c.toUpperCase();
  return c;
}

String effectiveIsPass() {
  String p = appConfig.isPass;
  p.trim();
  if (p.length() == 0) return String(aprsPasscode(effectiveIsCall()));
  return p;
}

bool getLocalTimeNow(struct tm &t) {
  if (!timeConfigured) return false;
  return getLocalTime(&t, 0);
}

void loadConfig() {
  prefs.begin("aprs", false);
  appConfig.callsign = prefs.getString("call", "N0CALL");
  appConfig.currentMode = prefs.getInt("mode", 0);
  appConfig.displayMode = prefs.getInt("dmode", 0);
  appConfig.dataFormat = prefs.getInt("dform", 0);
  appConfig.myLat = prefs.getFloat("lat", 52.1234);
  appConfig.myLon = prefs.getFloat("lon", 16.1234);
  appConfig.maxDistKm = prefs.getInt("dist", 50);
  appConfig.uartBaud = prefs.getInt("baud", 9600);
  appConfig.kissIP = prefs.getString("kip", "192.168.1.100");
  appConfig.kissPort = prefs.getInt("kport", 8001);
  appConfig.isServer = prefs.getString("isrv", "poland.aprs2.net");
  appConfig.isPort = prefs.getInt("isprt", 14580);
  appConfig.isCall = prefs.getString("iscall", "");
  appConfig.isPass = prefs.getString("ispass", "");
  appConfig.isFilter = prefs.getString("isflt", "");
  appConfig.wifiSSID = prefs.getString("wssid", "");
  appConfig.wifiPass = prefs.getString("wpass", "");
  appConfig.btAddress = prefs.getString("btmac", "98:D3:31:00:11:22");
  appConfig.btPin = prefs.getString("btpin", "1234");
  appConfig.beaconEnabled = prefs.getBool("ben", false);
  appConfig.beaconInterval = prefs.getInt("bint", 30);
  appConfig.beaconSymbol = prefs.getString("bsym", "/-");
  appConfig.beaconComment = prefs.getString("bcom", "https://github.com/SP3WRO/APRS_Display_System");
  appConfig.beaconPath = prefs.getString("bpath", "WIDE1-1,WIDE2-1");
  appConfig.hideNonPositional = prefs.getBool("hnp", false);
  appConfig.antiDuplicate = prefs.getBool("adup", false);
  appConfig.refreshInterval = prefs.getInt("refint", 7);
  appConfig.msgAck = prefs.getBool("mack", true);
  appConfig.apPass = prefs.getString("appass", "aprsdisplay");
  appConfig.webPass = prefs.getString("webpass", "");
  appConfig.timezone = prefs.getString("tz", "CET-1CEST,M3.5.0,M10.5.0/3");
  prefs.end();

  // Migracja z v1: stare wartości domyślne znaczyły "nie ustawiono"
  if (appConfig.isCall == "N0CALL") appConfig.isCall = "";
  if (appConfig.isPass == "12345") appConfig.isPass = "";
}

void startAccessPoint() {
  WiFi.mode(appConfig.wifiSSID.length() > 0 && !btFallbackTriggered ? WIFI_AP_STA : WIFI_AP);
  if (appConfig.apPass.length() >= 8) WiFi.softAP(AP_SSID, appConfig.apPass.c_str());
  else WiFi.softAP(AP_SSID);
  apActive = true;
  addToTerminal("Tryb AP: " + WiFi.softAPIP().toString());
}

// Krótka wersja na pasek startowy (pełna nazwa sieci nie zmieści się obok paska)
String apShortText() {
  if (appConfig.apPass.length() >= 8) return "AP hasło: " + appConfig.apPass;
  return "AP otwarte: 192.168.4.1";
}

String apInfoText() {
  String s = "Połącz się z siecią " AP_SSID;
  if (appConfig.apPass.length() >= 8) s += " (hasło: " + appConfig.apPass + ")";
  s += " i otwórz 192.168.4.1";
  return s;
}

String resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "włączenie zasilania";
    case ESP_RST_SW:        return "restart programowy";
    case ESP_RST_PANIC:     return "BŁĄD PROGRAMU (panic)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return "WATCHDOG (zawieszenie)";
    case ESP_RST_BROWNOUT:  return "SPADEK NAPIĘCIA (zasilanie)";
    case ESP_RST_EXT:       return "przycisk reset";
    case ESP_RST_DEEPSLEEP: return "wybudzenie";
    default:                return "nieznana (" + String((int)esp_reset_reason()) + ")";
  }
}

bool resetWasError() {
  esp_reset_reason_t r = esp_reset_reason();
  return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT || r == ESP_RST_BROWNOUT;
}

void setup() {
  Serial.begin(115200);
  display.init(0, true, 2, false);

  display.setRotation(0);
  display.setTextWrap(false);
  u8g2Fonts.begin(display);
  u8g2Fonts.setFontMode(1);        // przezroczyste tło - tekst na czarnych paskach
  u8g2Fonts.setFontDirection(0);
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);
  u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
  display.clearScreen();
  display.setFullWindow();

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.drawBitmap(0, 0, boot_logo, 400, 243, GxEPD_BLACK);
  } while (display.nextPage());

  // Przyczyna poprzedniego restartu - widoczna przy starcie i w terminalu panelu WWW
  String reason = resetReasonText();
  addToTerminal("Start po: " + reason);
  if (resetWasError()) {
    setBootProgress(2, "Poprzedni restart: " + reason);
    delay(3000);
  }

  setBootProgress(5, "Wczytywanie konfiguracji...");
  loadConfig();
  nextMsgId = (esp_random() % 900) + 1;

  // --- ODCZYT FLAGI AWARYJNEJ Z POPRZEDNIEJ SESJI ---
  prefs.begin("aprs", false);
  if (prefs.getBool("apFallback", false)) {
    prefs.putBool("apFallback", false); // Czyścimy flagę, by nie wpaść w pętlę przy kolejnym restarcie
    btFallbackTriggered = true;
  }
  prefs.end();

  lastRefreshTime = millis();
  // Pierwszy beacon minutę po starcie, kolejne co zadany interwał
  lastBeaconTime = millis() - (unsigned long)appConfig.beaconInterval * 60000UL + 60000UL;

  if (isBtMode()) {
    // NORMALNY START BLUETOOTH
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    setBootProgress(40, "Inicjalizacja Bluetooth...");
    SerialBT.begin("ESP32_Node", true);
    if (appConfig.btPin.length() > 0) {
      SerialBT.setPin(appConfig.btPin.c_str(), appConfig.btPin.length());
    }
    btConnectAttemptTime = millis();
    addToTerminal("Uruchomiono BT Master. Start odliczania...");
  } else {
    if (btFallbackTriggered) {
      // Fallback BT: od razu własna sieć AP, żeby dało się poprawić ustawienia.
      // Po BT_FALLBACK_RETRY_MS bez klientów urządzenie samo spróbuje BT ponownie.
      setBootProgress(30, "Brak połączenia BT. Uruchamiam AP...");
      startAccessPoint();
      setBootProgress(60, "Brak TNC BT! " + apShortText());
      delay(3000);
    } else {
      if (appConfig.wifiSSID.length() > 0) {
        setBootProgress(15, "Łączenie z WiFi: " + appConfig.wifiSSID);
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        WiFi.begin(appConfig.wifiSSID.c_str(), appConfig.wifiPass.c_str());
        // Bez odświeżania e-papieru w trakcie łączenia: szczyt poboru prądu radia razem
        // z odświeżaniem ekranu może przy słabym zasilaniu zrestartować ESP32 (brownout)
        int retries = 0;
        while (WiFi.status() != WL_CONNECTED && retries < 30) {
          delay(500);
          retries++;
        }
      }

      if (WiFi.status() != WL_CONNECTED) {
        // AP + dalsze próby łączenia z routerem w tle (np. router wolniej startuje po zaniku prądu)
        startAccessPoint();
        setBootProgress(70, "Brak WiFi. " + apShortText());
        delay(3000);
      } else {
        addToTerminal("WiFi OK! IP: " + WiFi.localIP().toString());
        setBootProgress(70, "WiFi OK! IP: " + WiFi.localIP().toString());
      }

      if (appConfig.wifiSSID.length() > 0) {
        configTzTime(appConfig.timezone.c_str(), "pool.ntp.org", "time.google.com");
        timeConfigured = true;
      }
    }

    setBootProgress(80, "Uruchamianie serwera WWW...");
    setupWebEndpoints();
    server.begin();
  }

  if (appConfig.currentMode == MODE_KISS_UART) {
    VPSerial.setRxBufferSize(VP_RX_BUFFER);
    VPSerial.begin(appConfig.uartBaud, SERIAL_8N1, VP_RX_PIN, VP_TX_PIN);
    addToTerminal("UART Gotowy (" + String(appConfig.uartBaud) + " bps)");
    setBootProgress(90, "UART Gotowy (" + String(appConfig.uartBaud) + " bps)");
  }

  setBootProgress(100, "Gotowe!");
  delay(1000);
  bootProgress = -1;   // dalsze komunikaty (np. łączenie BT) już bez paska

  // Bez WiFi na ekranie oczekiwania zostaje instrukcja, jak wejść do panelu
  String waiting = apActive ? "Brak WiFi.\n" + apInfoText() : String("Oczekuję na dane...");
  lastPacket.sourceCall = "";
  lastPacket.payload = waiting;
  lastPacket.rawPacket = waiting;
  lastPacket.parsedText = waiting;
  lastPacket.comment = waiting;
  lastPacket.distance = -1;
  lastPacket.bearing = -1;
  newDataToDisplay = true;
}

void loop() {
  if (!isBtMode()) server.handleClient();

  handleNetwork();
  handleMessages();

  unsigned long currentMillis = millis();

  if (emergencyActive && currentMillis - emergencyStart >= EMERGENCY_HOLD_MS) {
    emergencyActive = false;
  }

  if (newDataToDisplay && (currentMillis - lastDisplayUpdate >= 3000)) {
    lastDisplayUpdate = currentMillis;
    newDataToDisplay = false;
    updateDisplay();
  }

  if (appConfig.refreshInterval > 0 && (currentMillis - lastRefreshTime >= (appConfig.refreshInterval * 60000UL))) {
    lastRefreshTime = currentMillis;
    forceFullRefresh = true;
    newDataToDisplay = true;
    addToTerminal("> Automatyczne odswiezenie E-INK");
  }

  // Lista i statystyki pokazują zegar i "x min temu" - odświeżamy je co minutę
  static unsigned long lastStatsRedraw = 0;
  if (appConfig.displayMode != DISPLAY_CLASSIC && currentMillis - lastStatsRedraw >= 60000UL) {
    lastStatsRedraw = currentMillis;
    newDataToDisplay = true;
  }

  if (appConfig.beaconEnabled && appConfig.beaconInterval > 0) {
    if (currentMillis - lastBeaconTime >= (appConfig.beaconInterval * 60000UL)) {
      lastBeaconTime = currentMillis;
      transmitBeacon();
    }
  }

  if (appConfig.currentMode == MODE_KISS_UART) {
    while (VPSerial.available()) {
      parseKISS(VPSerial.read());
    }
  }

  // ZABEZPIECZENIE: Używaj SerialBT tylko jeśli nie działa tryb awaryjny
  if (isBtMode() && SerialBT.connected()) {
    int maxReads = 128;
    while (SerialBT.available() && maxReads-- > 0) {
      parseKISS(SerialBT.read());
    }
  }

  delay(10);
}
