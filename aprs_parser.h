// Parser ramek APRS - czysta logika bez zależności od sprzętu.
// Kompiluje się zarówno na ESP32, jak i na PC (testy w katalogu test/).
#pragma once
#include <Arduino.h>

struct APRSPacket {
  String sourceCall;
  String destCall;
  String path;
  String payload;
  String rawPacket;
  String parsedText;
  String comment;
  char symbolTable = '/';
  char symbolCode = 'L';
  float distance = -1;
  float bearing = -1;
  float lat = 0.0;
  float lon = 0.0;
  bool isEmergency = false;
  bool isWX = false;
  bool isObject = false;      // obiekt/item - pozycja nie jest pozycją nadawcy
  bool isThirdParty = false;  // przyszła przez iGate w ramce "}"
  String radioModel = "";

  // --- WIADOMOŚCI ---
  bool isMessage = false;
  bool isAck = false;      // ack/rej na naszą wiadomość
  bool isReject = false;
  String msgAddressee;
  String msgText;
  String msgId;

  unsigned long rxMillis = 0;
};

// --- PARSOWANIE ---
// Rozbija linię z APRS-IS (ZNAK>DEST,PATH:payload). Zwraca false dla komentarzy serwera.
bool parseAPRSISLine(const String &line, APRSPacket &pkt);
// Dekoduje ramkę AX.25 (bez bajtu komendy KISS).
bool parseAX25Frame(const uint8_t *buf, int len, APRSPacket &pkt);
// Dekoduje payload: pozycja, Mic-E, Base91, WX, PHG, wiadomości. Liczy dystans i azymut.
void parseAPRSPacket(APRSPacket &pkt, float myLat, float myLon);

// Czy ramka została odebrana bezpośrednio od nadawcy (bez digipeatera).
// RF: żaden adres w ścieżce nie ma bitu H ("*"). APRS-IS: bez "*" przed q-konstrukcją,
// czyli iGate słyszał stację bezpośrednio.
bool heardDirect(const APRSPacket &pkt);

// --- NARZĘDZIA ---
String identifyRadio(const String &tocall);
String identifyMicERadio(String &comment);
String removePolishChars(String input);
// Tekst do ekranu (UTF-8 z polskimi literami) i wersja czysto ASCII (do nadawania)
String toDisplayText(const String &input);
String toDisplayAscii(const String &input);
void cleanDAO(String &commentText);
bool isNumericStr(const String &str);
float calculateDistance(float lat1, float lon1, float lat2, float lon2);
float calculateBearing(float lat1, float lon1, float lat2, float lon2);
String getCompassDir(float bearing);
float parseCoord(const String &coordStr, bool isLat);
String decodeWeather(String &commentText);
String decodePHG(const String &payload);
bool decodeMicE(const String &dest, const String &info, float &lat, float &lon, char &symTable, char &symCode, int &speed, int &course, String &statusMsg);
bool decodeBase91(const String &payload, int start, float &lat, float &lon, char &symTable, char &symCode, int &commentOffset, String &extra);

// Passcode APRS-IS liczony ze znaku (bez SSID).
int aprsPasscode(String call);
// Znak wyrównany spacjami do 9 znaków (pole adresata wiadomości).
String padCall9(String call);
// Pozycja w formacie APRS: "5207.40N" / "01607.40E".
String formatAprsLat(float lat);
String formatAprsLon(float lon);
