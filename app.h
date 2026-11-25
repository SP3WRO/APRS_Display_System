// Wspólne typy i stałe programu (używane przez wszystkie zakładki .ino)
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "aprs_parser.h"

#define FW_VERSION "2.0"

enum AppMode { MODE_KISS_UART, MODE_KISS_TCP, MODE_APRS_IS, MODE_KISS_BT };
enum DisplayMode { DISPLAY_CLASSIC = 0, DISPLAY_LIST = 1, DISPLAY_STATS = 2 };

struct Config {
  String callsign = "N0CALL";
  int currentMode = MODE_KISS_UART;
  int displayMode = DISPLAY_CLASSIC;
  int dataFormat = 0;
  float myLat = 52.1234;
  float myLon = 16.1234;
  int maxDistKm = 50;

  int uartBaud = 9600;

  String kissIP = "192.168.1.100";
  int kissPort = 8001;
  String isServer = "poland.aprs2.net";
  int isPort = 14580;
  String isCall = "";       // puste = znak stacji
  String isPass = "";       // puste = liczony automatycznie
  String isFilter = "";     // puste = r/lat/lon/zasięg
  String wifiSSID = "";
  String wifiPass = "";

  String btAddress = "00:00:00:00:00:00";
  String btPin = "1234";

  bool beaconEnabled = false;
  int beaconInterval = 30;
  String beaconSymbol = "/-";
  String beaconComment = "APRS Display System";
  String beaconPath = "WIDE1-1,WIDE2-1";
  bool hideNonPositional = false;
  bool antiDuplicate = false;
  int refreshInterval = 60;

  bool msgAck = true;       // automatyczne ACK na wiadomości do nas
  String apPass = "aprsdisplay";
  String webPass = "";      // puste = panel bez hasła
  String timezone = "CET-1CEST,M3.5.0,M10.5.0/3";
};

// --- WIADOMOŚCI ---
struct MessageEntry {
  String from;
  String to;
  String text;
  String id;
  bool outgoing = false;
  bool acked = false;
  unsigned long ms = 0;
};

struct Outbox {
  bool active = false;
  String to;
  String text;
  String id;
  int tries = 0;
  unsigned long lastTry = 0;
};

// --- STATYSTYKI ---
struct HeardStation {
  String call;
  String text;
  String radio;
  float lat = 0;
  float lon = 0;
  float distance = -1;
  float bearing = -1;
  char symbolTable = '/';
  char symbolCode = 'L';
  unsigned long lastHeard = 0;
  unsigned long packets = 0;
};

const int MAX_HISTORY = 8;
const int MAX_TERM_LINES = 15;
const int MAX_SEEN = 20;
const int MAX_MESSAGES = 10;
const int MAX_HEARD = 64;

const unsigned long EMERGENCY_HOLD_MS = 60000UL;
const unsigned long APRSIS_TIMEOUT_MS = 90000UL;     // serwer wysyła "#" co ~20 s
const unsigned long WIFI_RETRY_MS = 30000UL;
const unsigned long AP_SHUTDOWN_MS = 60000UL;        // AP gaśnie minutę po odzyskaniu WiFi
const unsigned long BT_FALLBACK_RETRY_MS = 600000UL; // po 10 min w AP ponów BT
const unsigned long MSG_RETRY_MS = 30000UL;
const int MSG_MAX_TRIES = 5;

#define AP_SSID "APRS_DISPLAY_SETUP"
#define WIFI_TX_POWER WIFI_POWER_13dBm   // moc nadawania WiFi (mniejsze szczyty prądu)

// --- DEKLARACJE FUNKCJI Z ZAKŁADEK ---
// Jawne, bo automatyczne prototypy Arduino gubią się na długich stringach HTML.
void addToTerminal(String log);
void wifiModeLowPeak(wifi_mode_t mode);
bool isBtMode();
String effectiveIsCall();
String effectiveIsPass();
void loadConfig();
bool getLocalTimeNow(struct tm &t);
// display.ino
void updateDisplay();
void updateBootStatus(String text);
void setBootProgress(int percent, String text);
extern int bootProgress;
// network.ino
bool sendAPRSPayload(String source, String payload);
void transmitBeacon();
void parseKISS(uint8_t byte);
void handleNetwork();
String buildAprsIsFilter();
// messages.ino
bool handleIncomingMessage(APRSPacket &pkt);
String queueOutgoingMessage(String to, String text);
void handleMessages();
// stats.ino
void updateStats(const APRSPacket &pkt);
void updateDx(const APRSPacket &pkt);
int countHeardWithin(unsigned long windowMs);
String formatAge(unsigned long ms);
// web.ino
void setupWebEndpoints();
void applyLiveSettings(const Config &old, const String &oldFilter);
void handleDisplayMode();
