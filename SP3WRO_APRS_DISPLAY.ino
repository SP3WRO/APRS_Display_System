#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <WiFiClient.h>
#include "BluetoothSerial.h"
#include "icons.h"
#include <math.h>

// --- KONFIGURACJA EKRANU ---
#define ENABLE_GxEPD2_GFX 0
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

#define EPD_CS 5
#define EPD_DC 17
#define EPD_RST 16
#define EPD_BUSY 4
GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(GxEPD2_420_GDEY042T81(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// --- KONFIGURACJA UART ---
#define VP_RX_PIN 26 
#define VP_TX_PIN 27 
HardwareSerial VPSerial(2); 

// --- SERWER, KLIENT, BT ---
WebServer server(80);
Preferences prefs;
WiFiClient tcpClient; 
BluetoothSerial SerialBT;

enum AppMode { MODE_KISS_UART, MODE_KISS_TCP, MODE_APRS_IS, MODE_KISS_BT };

struct Config {
  String callsign = "N0CALL";
  int currentMode = MODE_KISS_UART;
  int displayMode = 0; 
  int dataFormat = 0; 
  float myLat = 52.1234;
  float myLon = 16.1234;
  int maxDistKm = 50;
  
  int uartBaud = 9600; 
  
  String kissIP = "192.168.1.100";
  int kissPort = 8001;
  String isServer = "poland.aprs2.net";
  int isPort = 14580;
  String isCall = "N0CALL";
  String isPass = "12345";
  String wifiSSID = "";
  String wifiPass = "";
  
  String btAddress = "00:00:00:00:00:00";
  String btPin = "1234";
  
  bool beaconEnabled = false;
  int beaconInterval = 30;
  String beaconSymbol = "/-"; 
  String beaconComment = "APRS Display System";
  bool hideNonPositional = false;
  bool antiDuplicate = false; 
  int refreshInterval = 60; 
} appConfig;

struct APRSPacket {
  String sourceCall;
  String destCall; 
  String path;
  String payload;
  String rawPacket; 
  String parsedText; 
  String comment; 
  char symbolTable; 
  char symbolCode;
  float distance; 
  float bearing;
  float lat;         
  float lon;
  bool isEmergency = false; 
  bool isWX = false; 
  String radioModel = ""; 
};

const int MAX_HISTORY = 8; 
APRSPacket history[MAX_HISTORY];
APRSPacket lastPacket;

const int MAX_TERM_LINES = 15;
String terminalLines[MAX_TERM_LINES];
int termIndex = 0;

// --- BUFOR ANTYDUPLIKATOWY ---
struct SeenStation {
  String call;
  String payload;
  unsigned long lastSeen;
};
const int MAX_SEEN = 20;
SeenStation seenStations[MAX_SEEN];
int seenIndex = 0;

// --- FLAGI I STOPERY ---
bool forceFullRefresh = true; 
unsigned long lastTcpRetry = 0; 
unsigned long lastRefreshTime = 0; 
unsigned long lastBeaconTime = 0; 
bool isisLoggedIn = false; 
bool newDataToDisplay = false;         
unsigned long lastDisplayUpdate = 0;
unsigned long emergencyUnlockTime = 0; 

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
uint8_t kissBuffer[512];
int kissIndex = 0;
bool kissEscaped = false;

// --- TERMINAL LOGGER ---
void addToTerminal(String log) {
  terminalLines[termIndex] = log;
  termIndex = (termIndex + 1) % MAX_TERM_LINES;
  Serial.println(log); 
}

String getTerminalText() {
  String txt = "";
  for(int i = 0; i < MAX_TERM_LINES; i++) {
    int idx = (termIndex + i) % MAX_TERM_LINES;
    if(terminalLines[idx].length() > 0) txt += terminalLines[idx] + "\n";
  }
  return txt;
}

// --- NARZĘDZIA TEKSTOWE ---

String identifyRadio(String tocall) {
  if (tocall.startsWith("APY008")) return "VX-8R";
  if (tocall.startsWith("APY01D")) return "FTM-100D";
  if (tocall.startsWith("APY02D")) return "FTM-200D";
  if (tocall.startsWith("APY03D")) return "FTM-300D";
  if (tocall.startsWith("APY04D")) return "FTM-400D";
  if (tocall.startsWith("APY05D")) return "FTM-500D";
  if (tocall.startsWith("APY1DR")) return "FT1D";
  if (tocall.startsWith("APY2DR")) return "FT2D";
  if (tocall.startsWith("APY3DR")) return "FT3D";
  if (tocall.startsWith("APY5DR")) return "FT5D";

  if (tocall.startsWith("APK003")) return "TH-D7";
  if (tocall.startsWith("APK004")) return "TM-D700";
  if (tocall.startsWith("APK005")) return "TM-D710";
  if (tocall.startsWith("APK006")) return "TH-D72";
  if (tocall.startsWith("APK007")) return "TM-D710G";
  if (tocall.startsWith("APK008")) return "TH-D74";
  if (tocall.startsWith("APK009")) return "TH-D75";

  if (tocall.startsWith("APA878")) return "AT-D878UV";
  if (tocall.startsWith("APA578")) return "AT-D578UV";

  if (tocall.startsWith("APLR")) return "LoRa Tracker";
  if (tocall.startsWith("APLORA")) return "LoRa Tracker";

  if (tocall.startsWith("APL")) return "Alinco";
  if (tocall.startsWith("APOX")) return "PicoAPRS";

  return ""; 
}

String identifyMicERadio(String &comment) {
  while(comment.length() > 0) {
    char c = comment.charAt(comment.length()-1);
    if (c < 32 || c > 126) {
        comment = comment.substring(0, comment.length() - 1);
    } else {
        break;
    }
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
  } 
  else if (firstChar == '>') {
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

void cleanDAO(String &commentText) {
  int daoW = commentText.indexOf("!W");
  if (daoW > 0 && daoW + 4 <= commentText.length() && commentText.charAt(daoW + 3) == '!') {
     commentText = commentText.substring(0, daoW);
  }
  int daow = commentText.indexOf("!w");
  if (daow > 0 && daow + 4 <= commentText.length() && commentText.charAt(daow + 3) == '!') {
     commentText = commentText.substring(0, daow);
  }
  commentText.trim();
}

// --- MATEMATYKA I KIERUNKI ---
float calculateDistance(float lat1, float lon1, float lat2, float lon2) {
  float R = 6371.0; 
  float dLat = (lat2 - lat1) * M_PI / 180.0;
  float dLon = (lon2 - lon1) * M_PI / 180.0;
  lat1 = lat1 * M_PI / 180.0;
  lat2 = lat2 * M_PI / 180.0;
  float a = sin(dLat/2) * sin(dLat/2) + sin(dLon/2) * sin(dLon/2) * cos(lat1) * cos(lat2);
  float c = 2 * atan2(sqrt(a), sqrt(1-a));
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
  const char* dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW", "N"};
  int idx = round(bearing / 45.0);
  if (idx < 0 || idx > 8) return "";
  return dirs[idx];
}

float parseCoord(String coordStr, bool isLat) {
  if (coordStr.length() < 7) return 0.0;
  float degrees = coordStr.substring(0, isLat ? 2 : 3).toFloat();
  float minutes = coordStr.substring(isLat ? 2 : 3, isLat ? 7 : 8).toFloat();
  float decimalDeg = degrees + (minutes / 60.0);
  char dir = coordStr.charAt(coordStr.length() - 1);
  if (dir == 'S' || dir == 'W') decimalDeg = -decimalDeg;
  return decimalDeg;
}

// --- DEKODERY ---
bool isNumericStr(String str) {
  for(int i=0; i<str.length(); i++) {
    if(!isDigit(str.charAt(i)) && str.charAt(i) != ' ' && str.charAt(i) != '-') return false;
  }
  return true;
}

String decodeWeather(String &commentText) {
  String out = "";
  bool hasData = false;
  
  int spaceIdx = commentText.indexOf(' ');
  String wxData = (spaceIdx >= 0) ? commentText.substring(0, spaceIdx) : commentText;
  String restComment = (spaceIdx >= 0) ? commentText.substring(spaceIdx + 1) : "";
  
  String dirStr = "", spdStr = "";
  
  if (wxData.length() >= 7 && wxData.charAt(3) == '/') {
    dirStr = wxData.substring(0, 3);
    spdStr = wxData.substring(4, 7);
  } 
  else if (wxData.indexOf('c') >= 0 && wxData.indexOf('s') >= 0) {
    int cIdx = wxData.indexOf('c');
    int sIdx = wxData.indexOf('s');
    if (cIdx + 3 < wxData.length() && sIdx + 3 < wxData.length()) {
      dirStr = wxData.substring(cIdx + 1, cIdx + 4);
      spdStr = wxData.substring(sIdx + 1, sIdx + 4);
    }
  } 
  else {
    int uscore = wxData.indexOf('_');
    if (uscore >= 0 && uscore + 8 <= wxData.length() && wxData.charAt(uscore + 4) == '/') {
      dirStr = wxData.substring(uscore + 1, uscore + 4);
      spdStr = wxData.substring(uscore + 5, uscore + 8);
    }
  }
  
  auto extractWx = [&](char tag, int len) -> String {
    int idx = wxData.indexOf(tag);
    if (idx >= 0 && idx + len < wxData.length()) {
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
        windLine += "" + dirStr + "st";
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
  if (hIdx >= 0 && hIdx + 2 <= wxData.length()) {
    String hStr = wxData.substring(hIdx + 1, hIdx + 3);
    if (isNumericStr(hStr)) {
      int hum = hStr.toInt();
      if (hum == 0) hum = 100;
      out += "Wilgotnosc: " + String(hum) + " %\n";
      hasData = true;
    }
  }
  
  String bStr = extractWx('b', 5);
  if (bStr != "") {
    float press = bStr.toFloat() / 10.0;
    out += "Cisn: " + String(press, 1) + " hPa\n";
    hasData = true;
  }
  
  String rStr = extractWx('r', 3);
  if (rStr != "") {
    float rMm = rStr.toFloat() * 0.254;
    out += "Opad(1h): " + String(rMm, 1) + " mm\n";
    hasData = true;
  }
  
  String pStr = extractWx('p', 3);
  if (pStr != "") {
    float pMm = pStr.toFloat() * 0.254;
    out += "Opad(24h): " + String(pMm, 1) + " mm\n";
    hasData = true;
  }
  
  String PStr = extractWx('P', 3);
  if (PStr != "") {
    float PMm = PStr.toFloat() * 0.254;
    out += "Opad(od 0:00): " + String(PMm, 1) + " mm\n";
    hasData = true;
  }
  
  if (!hasData) return "";
  
  out.trim();
  commentText = restComment; 
  return out;
}

String decodePHG(String payload) {
  int phgIdx = payload.indexOf("PHG");
  if (phgIdx >= 0 && phgIdx + 7 <= payload.length()) {
    String phgData = payload.substring(phgIdx + 3, phgIdx + 7);
    bool valid = true;
    for(int i = 0; i < 4; i++) {
      if(!isDigit(phgData.charAt(i))) valid = false;
    }
    if (valid) {
      int p = phgData.charAt(0) - '0';
      int h = phgData.charAt(1) - '0';
      int g = phgData.charAt(2) - '0';
      int d = phgData.charAt(3) - '0';

      int power = p * p;
      int heightM = round(10.0 * pow(2, h) * 0.3048);
      String dir = (d == 0) ? "Omni" : String(d * 45) + "st";

      return "[Radio] " + String(power) + "W, Ant: " + String(heightM) + "m, " + String(g) + "dB, " + dir;
    }
  }
  return "";
}

bool decodeMicE(String dest, String info, float &lat, float &lon, char &symTable, char &symCode, int &speed, int &course, String &statusMsg) {
  String dest6 = dest;
  int dashIdx = dest6.indexOf('-');
  if (dashIdx > 0) dest6 = dest6.substring(0, dashIdx);
  while(dest6.length() < 6) dest6 += " ";

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
  int n = 0, s = 0, w = 0, e = 0, latOff = 0; 

  for(int i=0; i<6; i++) {
    char ch = dest6.charAt(i);
    if(ch >= '0' && ch <= '9') dLat[i] = ch - '0';
    else if(ch >= 'A' && ch <= 'J') dLat[i] = ch - 'A';
    else if(ch >= 'P' && ch <= 'Y') dLat[i] = ch - 'P';
    else if(ch == 'K' || ch == 'L' || ch == 'Z') dLat[i] = 0;
    else return false; 
  }

  char c3 = dest6.charAt(3);
  if(c3 >= 'P' && c3 <= 'Z') n = 1; else s = 1;

  char c4 = dest6.charAt(4);
  if(c4 >= 'P' && c4 <= 'Z') w = 1; else e = 1;

  char c5 = dest6.charAt(5);
  if(c5 >= 'P' && c5 <= 'Z') latOff = 100;

  lat = (dLat[0]*10 + dLat[1]) + (dLat[2]*10 + dLat[3])/60.0 + (dLat[4]*10 + dLat[5])/6000.0;
  if (s) lat = -lat;

  int dLon = info.charAt(1) - 28;
  if(latOff) dLon += 100;
  if(dLon >= 180 && dLon <= 189) dLon -= 80;
  else if(dLon >= 190 && dLon <= 199) dLon -= 190;

  int mLon = info.charAt(2) - 28;
  if(mLon >= 60) mLon -= 60;

  int hLon = info.charAt(3) - 28;
  lon = dLon + mLon/60.0 + hLon/6000.0;
  if (w) lon = -lon;

  speed = 0; course = 0;
  int spdByte = info.charAt(4) - 28;
  int crsByte1 = info.charAt(5) - 28;
  int crsByte2 = info.charAt(6) - 28;
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

bool decodeBase91(String payload, float &lat, float &lon, char &symTable, char &symCode, int &commentOffset) {
  int compStart = -1;

  for (int i = 0; i < payload.length() - 13; i++) {
    char c = payload.charAt(i);
    if (c == '!' || c == '=') {
      compStart = i + 1;
      break;
    }
    else if (c == '/' || c == '@') {
      if (i + 8 + 9 <= payload.length()) { 
        compStart = i + 8;
        break;
      }
    }
  }

  if (compStart >= 0 && compStart + 10 <= payload.length()) {
    bool valid = true;
    for(int j = compStart + 1; j < compStart + 9; j++) {
       char b = payload.charAt(j);
       if (b < 33 || b > 124) { valid = false; break; }
    }

    if (valid) {
      symTable = payload.charAt(compStart);
      symCode = payload.charAt(compStart + 9);

      long y = 0;
      y += (long)(payload.charAt(compStart + 1) - 33) * 753571;
      y += (long)(payload.charAt(compStart + 2) - 33) * 8281;
      y += (long)(payload.charAt(compStart + 3) - 33) * 91;
      y += (long)(payload.charAt(compStart + 4) - 33);
      lat = 90.0 - ((float)y / 380926.0);

      long x = 0;
      x += (long)(payload.charAt(compStart + 5) - 33) * 753571;
      x += (long)(payload.charAt(compStart + 6) - 33) * 8281;
      x += (long)(payload.charAt(compStart + 7) - 33) * 91;
      x += (long)(payload.charAt(compStart + 8) - 33);
      lon = -180.0 + ((float)x / 190463.0);

      commentOffset = compStart + 12;
      if (payload.length() > commentOffset) {
          if (payload.charAt(commentOffset) != ' ') {
              commentOffset++; 
          }
      }
      return true;
    }
  }
  return false;
}

const unsigned char* getSymbolBitmap(char table, char code) {
  if (table == 'L' && (code == '#' || code == '&' || code == 'a' || code == '_')) return icon_lora; 
  if (code == '#') return icon_digi; 
  if (table == '/') {
    switch (code) {
      case 'V': return icon_v;    
      case '>': case 'F': case 'P': case 'R': case 'U': 
      case 'a': case 'f': case 'j': case 'k': case 'u': case 'v':
        return icon_car;           
      case '_': return icon_wx;            
      case '[': return icon_handheld;      
      case '-': return icon_home;          
      case ';': return icon_campground;    
      case 'O': return icon_balloon;       
      case 'r': return icon_repeater;      
      case 'S': return icon_shuttle;       
      case '<': return icon_motorcycle;    
      case 'L': return icon_lighthouse;    
    }
  } else if (table == '\\') {
    switch (code) {
      case 'u': case 'v': case 'k':
        return icon_car;
      case '-': return icon_home;          
      case ';': return icon_campground;    
      case '<': return icon_motorcycle;    
    }
  }
  return icon_lighthouse;
}

// --- LOGIKA NADAWANIA ---
void addCallsignToAX25(uint8_t* frame, int& len, String call, int defaultSsid, bool isLast) {
  int ssid = defaultSsid;
  int dashIdx = call.indexOf('-');
  if (dashIdx > 0) {
    ssid = call.substring(dashIdx + 1).toInt();
    call = call.substring(0, dashIdx);
  }
  call.toUpperCase();
  while (call.length() < 6) call += " "; 
  for (int i = 0; i < 6; i++) {
    frame[len++] = call.charAt(i) << 1; 
  }
  uint8_t ssidByte = 0x60 | ((ssid & 0x0F) << 1);
  if (isLast) ssidByte |= 0x01; 
  frame[len++] = ssidByte;
}

void sendKISSFrame(String payload) {
  uint8_t frame[256];
  int len = 0;

  addCallsignToAX25(frame, len, "APZESP", 0, false); 
  addCallsignToAX25(frame, len, appConfig.callsign, 0, false); 
  addCallsignToAX25(frame, len, "WIDE1", 1, false);  
  addCallsignToAX25(frame, len, "WIDE2", 1, true);   

  frame[len++] = 0x03; 
  frame[len++] = 0xF0; 

  for (int i = 0; i < payload.length() && len < 256; i++) {
    frame[len++] = payload.charAt(i);
  }

  auto writeByte = [&](uint8_t b) {
    if (appConfig.currentMode == MODE_KISS_UART) VPSerial.write(b);
    else if (appConfig.currentMode == MODE_KISS_TCP && tcpClient.connected()) tcpClient.write(b);
    else if (appConfig.currentMode == MODE_KISS_BT && !btFallbackTriggered && SerialBT.connected()) SerialBT.write(b);
  };

  auto writeKISS = [&](uint8_t b) {
    if (b == FEND) { writeByte(FESC); writeByte(TFEND); }
    else if (b == FESC) { writeByte(FESC); writeByte(TFESC); }
    else writeByte(b);
  };

  writeByte(FEND); writeKISS(0x00); 
  for(int i = 0; i < len; i++) writeKISS(frame[i]);
  writeByte(FEND); 

  addToTerminal("> TX KISS: " + payload);
}

void transmitBeacon() {
  if (!appConfig.beaconEnabled) return;

  char latStr[10], lonStr[10];
  float lat = abs(appConfig.myLat);
  float lon = abs(appConfig.myLon);

  int latDeg = (int)lat;
  float latMin = (lat - latDeg) * 60.0;
  snprintf(latStr, sizeof(latStr), "%02d%05.2f%c", latDeg, latMin, (appConfig.myLat >= 0) ? 'N' : 'S');

  int lonDeg = (int)lon;
  float lonMin = (lon - lonDeg) * 60.0;
  snprintf(lonStr, sizeof(lonStr), "%03d%05.2f%c", lonDeg, lonMin, (appConfig.myLon >= 0) ? 'E' : 'W');

  char symT = appConfig.beaconSymbol.length() > 0 ? appConfig.beaconSymbol.charAt(0) : '/';
  char symC = appConfig.beaconSymbol.length() > 1 ? appConfig.beaconSymbol.charAt(1) : '-';

  String payload = "=" + String(latStr) + symT + String(lonStr) + symC + appConfig.beaconComment;

  if (appConfig.currentMode == MODE_KISS_UART || appConfig.currentMode == MODE_KISS_TCP) {
     sendKISSFrame(payload); 
  } else if (appConfig.currentMode == MODE_KISS_BT && !btFallbackTriggered && SerialBT.connected()) {
     sendKISSFrame(payload);
  } else if (appConfig.currentMode == MODE_APRS_IS && tcpClient.connected()) {
     String isPkt = appConfig.callsign + ">APZESP,TCPIP*:=" + String(latStr) + symT + String(lonStr) + symC + appConfig.beaconComment + "\r\n";
     tcpClient.print(isPkt);
     addToTerminal("> TX IS: " + payload);
  }
}

void processParsedPacket(APRSPacket &pkt) {
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
  
  pkt.radioModel = identifyRadio(pkt.destCall);

  if (pkt.payload.length() == 0) return;

  String upperPayload = pkt.payload;
  upperPayload.toUpperCase();
  if (upperPayload.indexOf("EMERGENCY") >= 0) pkt.isEmergency = true;

  char dti = pkt.payload.charAt(0);

  if (dti == '}') {
    int innerColon = pkt.payload.indexOf(':');
    if (innerColon > 0) {
      int innerGreater = pkt.payload.indexOf('>');
      if (innerGreater > 1 && innerGreater < innerColon) pkt.sourceCall = pkt.payload.substring(1, innerGreater);
      pkt.payload = pkt.payload.substring(innerColon + 1);
      pkt.comment = pkt.payload;
      pkt.parsedText = pkt.payload; 
      dti = pkt.payload.charAt(0); 
    }
  }

  if (dti == '>') {
      pkt.comment = pkt.payload.substring(1);
      pkt.parsedText = "Status: " + pkt.comment;
  }

  if (dti == ':') {
    int colonIdx = pkt.payload.indexOf(':', 1);
    if (colonIdx > 0 && colonIdx <= 10) {
      String addressee = pkt.payload.substring(1, colonIdx);
      addressee.trim(); 
      if (addressee != appConfig.callsign) {
        addToTerminal("> UKRYTO SMS do " + addressee + ": " + pkt.sourceCall);
        return; 
      } else {
        pkt.parsedText = "[SMS] " + pkt.payload.substring(colonIdx + 1);
        pkt.comment = pkt.parsedText;
      }
    }
  }

  if (appConfig.antiDuplicate) {
    unsigned long currentMillis = millis();
    bool isDuplicate = false;
    for (int i = 0; i < MAX_SEEN; i++) {
      if (seenStations[i].call == pkt.sourceCall && seenStations[i].payload == pkt.payload) {
        if (currentMillis - seenStations[i].lastSeen < 10000) isDuplicate = true;
        seenStations[i].lastSeen = currentMillis; 
        break;
      }
    }
    
    if (isDuplicate) {
      addToTerminal("> DUPLIKAT: " + pkt.sourceCall);
      return; 
    } else {
      bool updated = false;
      for (int i = 0; i < MAX_SEEN; i++) {
        if (seenStations[i].call == pkt.sourceCall && seenStations[i].payload == pkt.payload) {
          seenStations[i].lastSeen = currentMillis; updated = true; break;
        }
      }
      if (!updated) {
        seenStations[seenIndex].call = pkt.sourceCall;
        seenStations[seenIndex].payload = pkt.payload;
        seenStations[seenIndex].lastSeen = currentMillis;
        seenIndex = (seenIndex + 1) % MAX_SEEN;
      }
    }
  }

  bool isPositional = (dti == '!' || dti == '=' || dti == '/' || dti == '@' || dti == ';' || dti == ')');
  bool isStandard = false;
  bool isMicE = false;
  bool isBase91 = false;

  if (dti == ';' || dti == ')') {
    int endIdx = -1;
    for(int i = 1; i <= 10; i++) {
       if(i < pkt.payload.length()) {
          char c = pkt.payload.charAt(i);
          if(c == '*' || c == '_' || c == '!') { endIdx = i; break; }
       }
    }
    if(endIdx > 1) {
      String objName = pkt.payload.substring(1, endIdx);
      objName.trim(); pkt.sourceCall = objName; 
    }
  }

  if (isPositional) {
    int dataStart = -1;
    for (int i = 7; i < (int)pkt.payload.length() - 11; i++) {
      char latDir = pkt.payload.charAt(i);
      if ((latDir == 'N' || latDir == 'S') && pkt.payload.charAt(i - 3) == '.') {
        char lonDir = pkt.payload.charAt(i + 10);
        if ((lonDir == 'E' || lonDir == 'W') && pkt.payload.charAt(i + 7) == '.') {
          dataStart = i - 7; break;
        }
      }
    }

    if (dataStart >= 0) {
      pkt.symbolTable = pkt.payload.charAt(dataStart + 8);
      pkt.symbolCode = pkt.payload.charAt(dataStart + 18);
      
      float pLat = parseCoord(pkt.payload.substring(dataStart, dataStart + 8), true);
      float pLon = parseCoord(pkt.payload.substring(dataStart + 9, dataStart + 18), false);
      
      if (pLat != 0.0 && pLon != 0.0) {
        pkt.lat = pLat; pkt.lon = pLon;
        pkt.distance = calculateDistance(appConfig.myLat, appConfig.myLon, pLat, pLon);
        pkt.bearing = calculateBearing(appConfig.myLat, appConfig.myLon, pLat, pLon);
        isStandard = true;
        
        if (pkt.payload.length() > dataStart + 19) pkt.comment = pkt.payload.substring(dataStart + 19);
        else pkt.comment = "";

        String spdCrsStr = "";
        if (pkt.payload.length() >= dataStart + 26 && pkt.payload.charAt(dataStart + 22) == '/') {
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
        if (aIdx > 0 && aIdx + 9 <= pkt.payload.length()) {
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
        float bLat = 0, bLon = 0;
        char bSymT, bSymC;
        int cOffset = 0; 
        
        if (decodeBase91(pkt.payload, bLat, bLon, bSymT, bSymC, cOffset)) {
            pkt.lat = bLat; pkt.lon = bLon;
            pkt.symbolTable = bSymT;
            pkt.symbolCode = bSymC;
            pkt.distance = calculateDistance(appConfig.myLat, appConfig.myLon, bLat, bLon);
            pkt.bearing = calculateBearing(appConfig.myLat, appConfig.myLon, bLat, bLon);
            isBase91 = true;
            
            if (pkt.payload.length() > cOffset) pkt.comment = pkt.payload.substring(cOffset);
            else pkt.comment = "";

            cleanDAO(pkt.comment);
            pkt.parsedText = "Base91 (" + String(bLat, 4) + ", " + String(bLon, 4) + ")\n" + pkt.comment;
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
          
          pkt.lat = mLat; pkt.lon = mLon;
          pkt.symbolTable = mSymT;
          pkt.symbolCode = mSymC;
          pkt.distance = calculateDistance(appConfig.myLat, appConfig.myLon, mLat, mLon);
          pkt.bearing = calculateBearing(appConfig.myLat, appConfig.myLon, mLat, mLon);
          
          String rawComment = "";
          if (pkt.payload.length() > 9) rawComment = pkt.payload.substring(9);
          
          String micEComment = rawComment;
          
          String micERadio = identifyMicERadio(micEComment); 
          if (micERadio.length() > 0) {
              pkt.radioModel = micERadio;
          }
          
          micEComment.trim();

          String extraInfo = "";

          int brac = -1;
          for (int i = 0; i < 6 && i < micEComment.length(); i++) {
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

          if (brac >= 0) micEComment = micEComment.substring(brac + 1);
          else {
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
          isMicE = true;
      }
  }
  
  if ((pkt.symbolTable == '/' || pkt.symbolTable == '\\' || isalnum(pkt.symbolTable)) && pkt.symbolCode == '_') {
      String restComment = pkt.comment; 
      String wxStr = decodeWeather(restComment);
      
      if (wxStr.length() > 0) {
          pkt.isWX = true; 
          
          pkt.parsedText = wxStr;
          
          if (restComment.length() > 0) {
              pkt.parsedText += "\nOpis: " + restComment;
          }
          
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
      if (phgText.length() > 0) {
          pkt.parsedText += "\n" + phgText; 
      }
  }

  pkt.comment = removePolishChars(pkt.comment);
  pkt.comment.trim(); 
  pkt.parsedText = removePolishChars(pkt.parsedText);

  if (appConfig.maxDistKm > 0 && pkt.distance > appConfig.maxDistKm) {
    addToTerminal("> ODRZUCONO (" + String(pkt.distance,0) + "km): " + pkt.sourceCall); 
    return; 
  }

  if (!pkt.isEmergency) {
    if (appConfig.hideNonPositional && pkt.distance < 0) {
      addToTerminal("> UKRYTO (Brak poz): " + pkt.sourceCall); 
      return; 
    }
  }

  if (millis() < emergencyUnlockTime && !pkt.isEmergency) {
    for (int i = MAX_HISTORY - 1; i > 0; i--) history[i] = history[i - 1];
    history[0] = pkt;
    addToTerminal("> Tlo (Alarm trw): " + pkt.sourceCall);
  } else {
    if (lastPacket.sourceCall.length() > 0 && lastPacket.sourceCall != "OCZEKUJE...") {
        for (int i = MAX_HISTORY - 1; i > 0; i--) history[i] = history[i - 1];
        history[0] = lastPacket;
    }
    lastPacket = pkt;        
    if (pkt.isEmergency) {
       emergencyUnlockTime = millis() + 60000UL; 
       forceFullRefresh = true;
       addToTerminal("> ALARM EMERGENCY!");
    }
  }
  newDataToDisplay = true; 
}

void updateDisplay() {
  if (forceFullRefresh) { display.clearScreen(); display.setFullWindow(); forceFullRefresh = false; } 
  else display.setPartialWindow(0, 0, 400, 300);

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    
    if (appConfig.displayMode == 0) {
      display.fillRect(0, 0, 400, 50, GxEPD_BLACK);
      display.setFont(&FreeSansBold18pt7b);
      display.setTextColor(GxEPD_WHITE);
      display.setCursor(10, 35);
      
      String topCall = lastPacket.sourceCall.length() > 0 ? lastPacket.sourceCall : "OCZEKUJE...";
      if (lastPacket.isEmergency && lastPacket.distance >= 0) topCall += " " + String(lastPacket.distance, 0) + "km";
      display.print(topCall);
      
      if (lastPacket.sourceCall.length() > 0) display.drawBitmap(350, 8, getSymbolBitmap(lastPacket.symbolTable, lastPacket.symbolCode), 32, 32, GxEPD_WHITE);
      display.setTextColor(GxEPD_BLACK);

      int cx = 75, cy = 125, r = 40;   
      display.drawCircle(cx, cy, r, GxEPD_BLACK);
      display.drawCircle(cx, cy, r-1, GxEPD_BLACK);
      display.drawLine(cx, cy - r, cx, cy + r, GxEPD_BLACK); 
      display.drawLine(cx - r, cy, cx + r, cy, GxEPD_BLACK); 
      
      display.setFont(&FreeMonoBold9pt7b);
      display.setCursor(cx - 5, cy - r - 5); display.print("N");
      display.setCursor(cx - 5, cy + r + 15); display.print("S");
      display.setCursor(cx + r + 5, cy + 4); display.print("E");
      display.setCursor(cx - r - 15, cy + 4); display.print("W");

      if (lastPacket.distance > 0 && lastPacket.bearing >= 0) {
        float rad = lastPacket.bearing * M_PI / 180.0;
        int dotX = cx + (sin(rad) * (r - 8));
        int dotY = cy - (cos(rad) * (r - 8));
        display.fillCircle(dotX, dotY, 6, GxEPD_BLACK);
      }

      display.setFont(&FreeSansBold12pt7b);
      if (lastPacket.distance >= 0 && !lastPacket.isEmergency) {
        String dStr = String(lastPacket.distance, 1) + " km";
        display.setCursor(cx - (dStr.length() * 5), cy + r + 35);
        display.print(dStr);
      }

      display.setFont(&FreeSans9pt7b);
      
      String pld = "";
      if (lastPacket.isEmergency) pld = lastPacket.parsedText; 
      else if (appConfig.dataFormat == 1) pld = lastPacket.rawPacket; 
      else if (appConfig.dataFormat == 2) pld = lastPacket.isWX ? lastPacket.parsedText : lastPacket.comment;
      else pld = lastPacket.parsedText;
      if (pld.length() == 0) pld = "(brak danych)";

      pld.replace("\r", "");

      int lineY = 85; 
      int maxY = lastPacket.isEmergency ? 165 : 205; 
      
      String currentLine = "";
      int i = 0;
      int maxCharsPerLine = 25; 
      
      while (i < pld.length() && lineY < maxY) {
        int nextSpace = pld.indexOf(' ', i);
        int nextNewline = pld.indexOf('\n', i);
        
        int wordEnd = -1;
        bool isNewline = false;
        
        if (nextNewline != -1 && (nextSpace == -1 || nextNewline < nextSpace)) {
          wordEnd = nextNewline;
          isNewline = true;
        } else if (nextSpace != -1) {
          wordEnd = nextSpace;
        } else {
          wordEnd = pld.length();
        }
        
        String word = pld.substring(i, wordEnd);
        
        if (word.length() > maxCharsPerLine) {
           if (currentLine.length() > 0) {
              display.setCursor(160, lineY); display.print(currentLine);
              lineY += 20; currentLine = "";
           }
           if (lineY < maxY) {
              display.setCursor(160, lineY); display.print(word.substring(0, maxCharsPerLine));
              lineY += 20; word = word.substring(maxCharsPerLine);
           }
        }
        
        if (currentLine.length() + word.length() + (currentLine.length() > 0 ? 1 : 0) <= maxCharsPerLine) {
          if (currentLine.length() > 0) currentLine += " ";
          currentLine += word;
        } else {
          display.setCursor(160, lineY);
          display.print(currentLine);
          lineY += 20;
          currentLine = word;
        }
        
        if (isNewline && lineY < maxY) {
           display.setCursor(160, lineY);
           display.print(currentLine);
           lineY += 20;
           currentLine = "";
        }
        
        i = wordEnd + 1; 
      }
      
      if (currentLine.length() > 0 && lineY < maxY) {
        display.setCursor(160, lineY);
        display.print(currentLine);
      }

      if (lastPacket.isEmergency) {
        display.fillRect(0, 170, 400, 35, GxEPD_BLACK);
        display.setFont(&FreeSansBold18pt7b);
        display.setTextColor(GxEPD_WHITE);
        display.setCursor(95, 196);
        display.print("EMERGENCY!");
        display.setTextColor(GxEPD_BLACK);
      }

      display.drawLine(0, 205, 400, 205, GxEPD_BLACK);
      display.drawLine(250, 205, 250, 250, GxEPD_BLACK); 
      
      display.setFont(&FreeMonoBold9pt7b);
      int yPos = 225;
      
      for (int hist_i = 0; hist_i < 2; hist_i++) {
        if (history[hist_i].sourceCall.length() > 0) {
          display.setCursor(5, yPos);
          String histLine = history[hist_i].sourceCall;
          if(history[hist_i].distance >= 0) histLine += " (" + String(history[hist_i].distance, 0) + "km)";
          display.print(histLine);
          yPos += 22; 
        }
      }

      if (lastPacket.radioModel.length() > 0) {
        display.setFont(&FreeSansBold12pt7b);
        display.setCursor(255, 240); 
        display.print(lastPacket.radioModel);
      }

    } else {
      display.fillRect(0, 0, 400, 30, GxEPD_BLACK);
      display.setFont(&FreeSansBold12pt7b);
      display.setTextColor(GxEPD_WHITE);
      display.setCursor(5, 22);
      
      if (lastPacket.isEmergency) display.print("ALARM RATUNKOWY!");
      else display.print("LISTA STACJI APRS");
      display.setTextColor(GxEPD_BLACK);

      int yPos = 50;
      int maxLimit = lastPacket.isEmergency ? 220 : 260; 
      
      for (int i = -1; i < MAX_HISTORY; i++) {
        APRSPacket* p = (i == -1) ? &lastPacket : &history[i];
        if (p->sourceCall.length() == 0) continue;
        if (yPos > maxLimit) break; 
        
        display.setFont(&FreeMonoBold9pt7b);
        display.setCursor(5, yPos);
        String header = p->sourceCall;
        
        if (p->distance >= 0) {
            header += " [" + String(p->distance, 0) + "km]";
            if (p->bearing >= 0) {
                header += " [" + getCompassDir(p->bearing) + "]";
            }
        }
        
        if (p->radioModel.length() > 0) {
            header += " [" + p->radioModel + "]";
        }
        
        if (p->isEmergency) header = "! " + header; 
        display.print(header);
        
        yPos += 18;
        display.setFont(&FreeSans9pt7b);
        display.setCursor(15, yPos);
        
        String pld = "";
        if (p->isEmergency) pld = p->parsedText; 
        else if (appConfig.dataFormat == 1) pld = p->rawPacket;
        else if (appConfig.dataFormat == 2) pld = p->isWX ? p->parsedText : p->comment;
        else pld = p->parsedText;
        if (pld.length() == 0) pld = "(brak danych)";
        
        pld.replace("\r", "");
        pld.replace('\n', ' '); 
        if (pld.length() > 38) pld = pld.substring(0, 35) + "..."; 
        
        display.print(pld);
        yPos += 20;
        display.drawLine(5, yPos - 13, 395, yPos - 13, GxEPD_BLACK);
      }

      if (lastPacket.isEmergency) {
        display.fillRect(0, 235, 400, 35, GxEPD_BLACK);
        display.setFont(&FreeSansBold18pt7b);
        display.setTextColor(GxEPD_WHITE);
        display.setCursor(95, 261);
        display.print("EMERGENCY!");
        display.setTextColor(GxEPD_BLACK);
      }
    }

    display.fillRect(0, 275, 400, 25, GxEPD_BLACK);
    display.setFont(&FreeSans9pt7b);
    display.setTextColor(GxEPD_WHITE);
    display.setCursor(5, 293);
    
    String modeStr = "";
    String ipStr = "Brak";
    
    if (appConfig.currentMode == MODE_KISS_UART) {
      modeStr = "VP-DIGI (" + String(appConfig.uartBaud) + ")";
      ipStr = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "Brak WiFi";
    }
    else if (appConfig.currentMode == MODE_KISS_TCP) {
      modeStr = "KISS TCP";
      ipStr = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "Brak WiFi";
    }
    else if (appConfig.currentMode == MODE_APRS_IS) {
      modeStr = "APRS-IS";
      ipStr = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "Brak WiFi";
    }
    else if (appConfig.currentMode == MODE_KISS_BT) {
      modeStr = "Bluetooth SPP";
      if (btFallbackTriggered) {
          ipStr = "AP: 192.168.4.1";
      } else {
          ipStr = SerialBT.connected() ? "Polaczono" : "Szukam HC-05...";
      }
    }
    
    display.print(appConfig.callsign + " | " + modeStr + " | " + ipStr);

  } while (display.nextPage());
}

String extractCallsign(uint8_t* buffer, int startOffset) {
  String call = "";
  for (int i = 0; i < 6; i++) {
    char c = buffer[startOffset + i] >> 1;
    if (c != ' ') call += c;
  }
  uint8_t ssid = (buffer[startOffset + 6] >> 1) & 0x0F;
  if (ssid > 0) call += "-" + String(ssid);
  return call;
}

void processAX25(int length) {
  if (length < 16 || kissBuffer[0] != 0x00) return;
  APRSPacket pkt;
  
  pkt.destCall = extractCallsign(kissBuffer, 1);
  pkt.sourceCall = extractCallsign(kissBuffer, 8);
  
  int currentByte = 1;
  while (currentByte < length - 1) {
    if (kissBuffer[currentByte + 6] & 0x01) { currentByte += 7; break; }
    currentByte += 7;
  }
  
  int pathStart = 15;
  while(pathStart < currentByte - 7) {
     pkt.path += extractCallsign(kissBuffer, pathStart) + ",";
     pathStart += 7;
  }
  if(pkt.path.endsWith(",")) pkt.path = pkt.path.substring(0, pkt.path.length()-1);
  if(pkt.path == "") pkt.path = "LOCAL";

  int infoStart = currentByte + 2; 
  for (int i = infoStart; i < length; i++) {
    char c = (char)kissBuffer[i];
    if ((c >= 28 && c <= 126) || c == '\n' || c == '\r') pkt.payload += c;
  }
  
  pkt.rawPacket = pkt.sourceCall + ">" + pkt.destCall;
  if (pkt.path.length() > 0 && pkt.path != "LOCAL") pkt.rawPacket += "," + pkt.path;
  pkt.rawPacket += ":" + pkt.payload;

  addToTerminal("RX KISS: " + pkt.rawPacket);
  processParsedPacket(pkt);
}

void parseKISS(uint8_t byte) {
  if (byte == FEND) {
    if (kissIndex > 0) processAX25(kissIndex);
    kissIndex = 0; kissEscaped = false;
    return;
  }
  if (kissEscaped) {
    if (byte == TFEND) byte = FEND;
    if (byte == TFESC) byte = FESC;
    kissEscaped = false;
  } else if (byte == FESC) {
    kissEscaped = true; return;
  }
  if (kissIndex < 512) kissBuffer[kissIndex++] = byte;
}

void parseAPRSIS(String line) {
  addToTerminal("IS: " + line);
  if (line.indexOf("logresp") >= 0) {
    isisLoggedIn = true;
    if (line.indexOf("unverified") >= 0) addToTerminal("UWAGA: Haslo IS odrzucone!");
    else addToTerminal("APRS-IS Autoryzacja OK!");
  }

  if(line.startsWith("#") || line.length() < 10) return; 
  
  APRSPacket pkt;
  pkt.rawPacket = line; 
  int pos1 = line.indexOf('>');
  int pos2 = line.indexOf(':');
  if(pos1 > 0 && pos2 > pos1) {
    pkt.sourceCall = line.substring(0, pos1);
    int commaPos = line.indexOf(',', pos1);
    if(commaPos > 0 && commaPos < pos2) {
        pkt.destCall = line.substring(pos1 + 1, commaPos);
        pkt.path = line.substring(commaPos + 1, pos2);
    } else {
        pkt.destCall = line.substring(pos1 + 1, pos2);
        pkt.path = "";
    }
    pkt.payload = line.substring(pos2 + 1);
    processParsedPacket(pkt);
  }
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
  appConfig.isCall = prefs.getString("iscall", "N0CALL");
  appConfig.isPass = prefs.getString("ispass", "12345");
  appConfig.wifiSSID = prefs.getString("wssid", "");
  appConfig.wifiPass = prefs.getString("wpass", "");
  appConfig.btAddress = prefs.getString("btmac", "98:D3:31:00:11:22");
  appConfig.btPin = prefs.getString("btpin", "1234");
  appConfig.beaconEnabled = prefs.getBool("ben", false);
  appConfig.beaconInterval = prefs.getInt("bint", 30);
  appConfig.beaconSymbol = prefs.getString("bsym", "/-");
  appConfig.beaconComment = prefs.getString("bcom", "https://github.com/SP3WRO/APRS_Display_System");
  appConfig.hideNonPositional = prefs.getBool("hnp", false);
  appConfig.antiDuplicate = prefs.getBool("adup", false); 
  appConfig.refreshInterval = prefs.getInt("refint", 7); 
  prefs.end();
}

void setupWebEndpoints() {
  server.on("/", []() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    
    server.sendContent(R"rawliteral(
    <!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body { font-family: Arial; padding: 10px; background: #f0f2f5; }
      .box { background: #fff; padding: 15px; border-radius: 8px; margin-bottom: 15px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
      input[type=text], input[type=password], input[type=number], select { width: 100%; padding: 8px; margin: 5px 0 15px 0; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; }
      textarea { width: 100%; height: 200px; background: #000; color: #0f0; font-family: monospace; padding: 10px; box-sizing: border-box; }
      .btn { background: #007BFF; color: #fff; padding: 10px; border: none; width: 100%; border-radius: 4px; font-size: 16px; cursor: pointer; }
      h3 { margin-top: 0; }
      #dform_msg { color: #d9534f; font-weight: bold; margin-top: 5px; font-size: 13px; }
    </style>
    <script>
      function toggle() {
        var m = document.getElementById('mode').value;
        document.getElementById('m_uart').style.display = (m==0) ? 'block' : 'none';
        document.getElementById('m_tcp').style.display  = (m==1) ? 'block' : 'none';
        document.getElementById('m_is').style.display   = (m==2) ? 'block' : 'none';
        document.getElementById('m_bt').style.display   = (m==3) ? 'block' : 'none';
        
        var dmode = document.getElementById('dmode').value;
        var dform = document.getElementById('dform');
        var dformMsg = document.getElementById('dform_msg');
        
        if (dmode == '1') {
           dform.value = '2'; 
           dform.disabled = true;
           if(dformMsg) dformMsg.style.display = 'block';
        } else {
           dform.disabled = false;
           if(dformMsg) dformMsg.style.display = 'none';
        }
      }
      setInterval(function() {
        fetch('/terminal').then(r => r.text()).then(t => {
          var ta = document.getElementById('term');
          var isScrolledToBottom = ta.scrollHeight - ta.clientHeight <= ta.scrollTop + 1;
          ta.value = t;
          if(isScrolledToBottom) ta.scrollTop = ta.scrollHeight;
        });
      }, 2000);
      window.onload = toggle;
    </script>
    </head><body><h2>APRS Display System Panel</h2>
    <form action="/save" method="POST">
    )rawliteral");

    String chunk = "<div class=\"box\"><h3>Wygląd i tryb pracy ekranu</h3><b>Tryb Wyświetlania:</b> <select id=\"dmode\" name=\"dmode\" onchange=\"toggle()\">";
    chunk += "<option value=\"0\" " + String(appConfig.displayMode == 0 ? "selected" : "") + ">Klasyczny (Kompas + 2 stacje)</option>";
    chunk += "<option value=\"1\" " + String(appConfig.displayMode == 1 ? "selected" : "") + ">Lista Stacji (Do 6 obiektów)</option></select><br>";
    
    chunk += "<b>Format Danych:</b> <select id=\"dform\" name=\"dform\">";
    chunk += "<option value=\"0\" " + String(appConfig.dataFormat == 0 ? "selected" : "") + ">Zdekodowane (Współrzędne/Telemetria)</option>";
    chunk += "<option value=\"1\" " + String(appConfig.dataFormat == 1 ? "selected" : "") + ">Surowe (Raw Payload)</option>";
    chunk += "<option value=\"2\" " + String(appConfig.dataFormat == 2 ? "selected" : "") + ">Tylko Komentarz (Wyjątek: WX)</option></select>";
    chunk += "<div id=\"dform_msg\" style=\"display:none;\">* W trybie listy automatycznie wymuszono pokazywanie samego komentarza.</div></div>";
    server.sendContent(chunk);

    chunk = "<div class=\"box\"><h3>Sieć WiFi</h3>SSID: <input type=\"text\" name=\"wssid\" value=\"" + appConfig.wifiSSID + "\">";
    chunk += "Hasło: <input type=\"password\" name=\"wpass\" value=\"" + appConfig.wifiPass + "\"></div>";
    server.sendContent(chunk);

    chunk = "<div class=\"box\"><h3>Stacja & Filtry</h3>Znak: <input type=\"text\" name=\"call\" value=\"" + appConfig.callsign + "\">";
    chunk += "Szerokość (Lat): <input type=\"text\" name=\"lat\" value=\"" + String(appConfig.myLat, 4) + "\">";
    chunk += "Długość (Lon): <input type=\"text\" name=\"lon\" value=\"" + String(appConfig.myLon, 4) + "\">";
    chunk += "Maks. Odległość (km, 0=wył): <input type=\"number\" name=\"dist\" value=\"" + String(appConfig.maxDistKm) + "\"><br><br>";
    chunk += "<input type=\"checkbox\" name=\"hnp\" value=\"1\" " + String(appConfig.hideNonPositional ? "checked" : "") + "> <b>Ukryj ramki bez pozycji</b><br><br>";
    chunk += "<input type=\"checkbox\" name=\"adup\" value=\"1\" " + String(appConfig.antiDuplicate ? "checked" : "") + "> <b>Blokuj duplikaty (10s)</b><br><br>";
    chunk += "Pełne odświeżanie e-papieru (min): <input type=\"number\" name=\"refint\" value=\"" + String(appConfig.refreshInterval) + "\"><br><br>";
    server.sendContent(chunk);

    chunk = "<b>Złącze Danych:</b> <select id=\"mode\" name=\"mode\" onchange=\"toggle()\">";
    chunk += "<option value=\"0\" " + String(appConfig.currentMode == 0 ? "selected" : "") + ">UART TNC (VP-Digi)</option>";
    chunk += "<option value=\"1\" " + String(appConfig.currentMode == 1 ? "selected" : "") + ">KISS TCP (Klient)</option>";
    chunk += "<option value=\"2\" " + String(appConfig.currentMode == 2 ? "selected" : "") + ">APRS-IS (Internet)</option>";
    chunk += "<option value=\"3\" " + String(appConfig.currentMode == 3 ? "selected" : "") + ">Bluetooth SPP (Klient)</option></select></div>";
    server.sendContent(chunk);

    chunk = "<div class=\"box\" style=\"background-color: #eaf4ff;\"><h3>Nadawanie Beacona</h3>";
    chunk += "<input type=\"checkbox\" name=\"ben\" value=\"1\" " + String(appConfig.beaconEnabled ? "checked" : "") + "> <b>Włącz beacona</b><br><br>";
    chunk += "Odstęp (Minuty): <input type=\"number\" name=\"bint\" value=\"" + String(appConfig.beaconInterval) + "\">";
    chunk += "Symbol (2 znaki np. /-): <input type=\"text\" name=\"bsym\" maxlength=\"2\" value=\"" + appConfig.beaconSymbol + "\">";
    chunk += "Komentarz: <input type=\"text\" name=\"bcom\" value=\"" + appConfig.beaconComment + "\"></div>";
    server.sendContent(chunk);

    chunk = "<div id=\"m_uart\" class=\"box\" style=\"display:none;\"><h3>Tryb: VP-Digi UART</h3>";
    chunk += "Piny połączone sprzętowo (TX:27, RX:26)<br><br><b>Prędkość (Baudrate):</b> <select name=\"baud\">";
    chunk += "<option value=\"1200\" " + String(appConfig.uartBaud == 1200 ? "selected" : "") + ">1200 bps</option>";
    chunk += "<option value=\"4800\" " + String(appConfig.uartBaud == 4800 ? "selected" : "") + ">4800 bps</option>";
    chunk += "<option value=\"9600\" " + String(appConfig.uartBaud == 9600 ? "selected" : "") + ">9600 bps</option>";
    chunk += "<option value=\"19200\" " + String(appConfig.uartBaud == 19200 ? "selected" : "") + ">19200 bps</option>";
    chunk += "<option value=\"38400\" " + String(appConfig.uartBaud == 38400 ? "selected" : "") + ">38400 bps</option>";
    chunk += "<option value=\"115200\" " + String(appConfig.uartBaud == 115200 ? "selected" : "") + ">115200 bps</option></select></div>";
    server.sendContent(chunk);

    chunk = "<div id=\"m_tcp\" class=\"box\" style=\"display:none;\"><h3>Tryb: KISS TCP</h3>IP: <input type=\"text\" name=\"kip\" value=\"" + appConfig.kissIP + "\"> Port: <input type=\"number\" name=\"kport\" value=\"" + String(appConfig.kissPort) + "\"></div>";
    chunk += "<div id=\"m_is\" class=\"box\" style=\"display:none;\"><h3>Tryb: APRS-IS</h3>Serwer: <input type=\"text\" name=\"isrv\" value=\"" + appConfig.isServer + "\"> Port: <input type=\"number\" name=\"isprt\" value=\"" + String(appConfig.isPort) + "\"> Znak: <input type=\"text\" name=\"iscall\" value=\"" + appConfig.isCall + "\"> Passcode: <input type=\"text\" name=\"ispass\" value=\"" + appConfig.isPass + "\"></div>";
    chunk += "<div id=\"m_bt\" class=\"box\" style=\"display:none;\"><h3>Tryb: Bluetooth SPP (Klient)</h3>Adres MAC (Wymagany, np. 98:D3:31...): <input type=\"text\" name=\"btmac\" value=\"" + appConfig.btAddress + "\"> PIN (domyślnie 1234 lub 0000): <input type=\"text\" name=\"btpin\" value=\"" + appConfig.btPin + "\"></div>";
    server.sendContent(chunk);

    server.sendContent("<input type=\"submit\" class=\"btn\" value=\"Zapisz i Zrestartuj\"></form>");
    server.sendContent("<div class=\"box\" style=\"margin-top:15px;\"><h3>Terminal na żywo</h3><textarea id=\"term\" readonly></textarea></div></body></html>");
    server.sendContent(""); 
  });

  server.on("/save", HTTP_POST, []() {
    prefs.begin("aprs", false);
    
    int savedDmode = server.arg("dmode").toInt();
    int savedDform = server.arg("dform").toInt();
    if (savedDmode == 1) savedDform = 2;

    prefs.putInt("dmode", savedDmode);
    prefs.putInt("dform", savedDform);
    prefs.putString("call", server.arg("call"));
    prefs.putFloat("lat", server.arg("lat").toFloat());
    prefs.putFloat("lon", server.arg("lon").toFloat());
    prefs.putInt("dist", server.arg("dist").toInt());
    prefs.putInt("mode", server.arg("mode").toInt());
    prefs.putInt("baud", server.arg("baud").toInt()); 
    prefs.putString("kip", server.arg("kip"));
    prefs.putInt("kport", server.arg("kport").toInt());
    prefs.putString("isrv", server.arg("isrv"));
    prefs.putInt("isprt", server.arg("isprt").toInt());
    prefs.putString("iscall", server.arg("iscall"));
    prefs.putString("ispass", server.arg("ispass"));
    prefs.putString("wssid", server.arg("wssid"));
    prefs.putString("wpass", server.arg("wpass"));
    prefs.putString("btmac", server.arg("btmac"));
    prefs.putString("btpin", server.arg("btpin"));
    prefs.putBool("ben", server.hasArg("ben"));
    prefs.putInt("bint", server.arg("bint").toInt());
    prefs.putString("bsym", server.arg("bsym"));
    prefs.putString("bcom", server.arg("bcom"));
    prefs.putBool("hnp", server.hasArg("hnp"));
    prefs.putBool("adup", server.hasArg("adup")); 
    prefs.putInt("refint", server.arg("refint").toInt()); 
    prefs.end();
    
    server.send(200, "text/html", "Zapisano. Trwa restart...");
    delay(1000);
    ESP.restart();
  });

  server.on("/terminal", HTTP_GET, []() {
    server.send(200, "text/plain", getTerminalText());
  });
}

void handleNetwork() {
  unsigned long currentMillis = millis();

  // Logika łączenia awaryjnego w przypadku awarii Bluetooth
  if (appConfig.currentMode == MODE_KISS_BT && !btFallbackTriggered) {
    if (!SerialBT.connected()) {
        
        String macStr = appConfig.btAddress;
        macStr.replace(":", "");
        macStr.replace("-", ""); // Na wypadek użycia myślników
        macStr.trim();
        
        bool invalidMac = (macStr.length() != 12);
        
        // Twardy fallback natychmiast przy złym MAC lub po 3 nieudanych próbach
        if (currentMillis - btConnectAttemptTime > 60000UL || invalidMac || btFailedAttempts >= 3) {
            
            if (invalidMac) addToTerminal("BT: Zly format MAC! Twardy fallback!");
            else addToTerminal("BT: Brak modulu. Twardy fallback do AP!");
            
            prefs.begin("aprs", false);
            prefs.putBool("apFallback", true);
            prefs.end();
            
            delay(500);
            ESP.restart(); // Bezpieczny restart omijający Core Panic
            
        } else if (currentMillis - lastBtRetry > 10000 || lastBtRetry == 0) {
            
            btFailedAttempts++;
            uint8_t mac[6];
            
            addToTerminal("BT: Laczenie z MAC (Proba " + String(btFailedAttempts) + "/3)...");
            updateBootStatus("BT: Laczenie... (" + String(btFailedAttempts) + "/3)");
            
            for(int i=0; i<6; i++) {
                mac[i] = strtol(macStr.substring(i*2, i*2+2).c_str(), NULL, 16);
            }
            
            bool success = SerialBT.connect(mac);
            
            if (success) {
                addToTerminal("BT: Polaczono z HC-05!");
                updateBootStatus("BT: Polaczono!");
                forceFullRefresh = true;
                newDataToDisplay = true;
                btConnectAttemptTime = millis(); 
                btFailedAttempts = 0; // Kasujemy błędy po sukcesie
            } else {
                addToTerminal("BT: Blad polaczenia.");
                delay(100); // Krótki oddech dla FreeRTOS
                SerialBT.disconnect(); // Oczyszczenie stanu maszyny BT! (Kluczowe dla ochrony)
            }
            lastBtRetry = millis(); 
        }
    } else {
        btConnectAttemptTime = currentMillis;
        btFailedAttempts = 0; // Gdy zrywa stabilne połączenie, przyznajemy nowe 3 szanse
    }
  }

  // Obsługa połączeń w trybach WiFi
  if (appConfig.currentMode == MODE_KISS_TCP || appConfig.currentMode == MODE_APRS_IS) {
    if (WiFi.status() == WL_CONNECTED && !tcpClient.connected()) {
      if (currentMillis - lastTcpRetry > 5000 || lastTcpRetry == 0) {
        lastTcpRetry = currentMillis;
        isisLoggedIn = false; 
        
        addToTerminal("Laczenie z serwerem TCP...");
        String host = (appConfig.currentMode == MODE_KISS_TCP) ? appConfig.kissIP : appConfig.isServer;
        int port = (appConfig.currentMode == MODE_KISS_TCP) ? appConfig.kissPort : appConfig.isPort;
        
        if (tcpClient.connect(host.c_str(), port)) {
          addToTerminal("Polaczono z " + host);
          if (appConfig.currentMode == MODE_APRS_IS) {
            String filterStr = "";
            if (appConfig.maxDistKm > 0 && appConfig.myLat != 0.0) {
                filterStr = " filter r/" + String(appConfig.myLat, 4) + "/" + String(appConfig.myLon, 4) + "/" + String(appConfig.maxDistKm);
            }
            String login = "user " + appConfig.isCall + " pass " + appConfig.isPass + " vers ESP32_Node 2.0" + filterStr + "\r\n";
            tcpClient.print(login);
            addToTerminal("Przeslano autoryzacje.");
          } else {
            isisLoggedIn = true; 
          }
        } else {
          addToTerminal("Odrzucono. Ponawiam...");
        }
      }
    }
  }

  // Odczyty z TCP
  if ((appConfig.currentMode == MODE_KISS_TCP || appConfig.currentMode == MODE_APRS_IS) && tcpClient.connected()) {
    static String tcpBuffer = "";
    while (tcpClient.available()) {
       char c = tcpClient.read();
       if (appConfig.currentMode == MODE_KISS_TCP) parseKISS(c);
       else if (appConfig.currentMode == MODE_APRS_IS) {
         if (c == '\n') {
           tcpBuffer.trim();
           if (tcpBuffer.length() > 0) parseAPRSIS(tcpBuffer);
           tcpBuffer = "";
         } else if (c != '\r' && tcpBuffer.length() < 256) {
           tcpBuffer += c; 
         }
       }
    }
  }
}

// Funkcja aktualizująca tekst podczas bootowania w dolnej części ekranu
void updateBootStatus(String text) {
  display.setPartialWindow(0, 243, 400, 57);
  display.firstPage();
  do {
    display.fillRect(0, 243, 400, 57, GxEPD_WHITE);
    display.setFont(&FreeSans9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(10, 276); 
    display.print(text);
  } while (display.nextPage());
}

void setup() {
  Serial.begin(115200);
  display.init(0, true, 2, false);
  
  display.setRotation(0);
  display.setTextWrap(false);
  display.clearScreen(); 
  display.setFullWindow();
  
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.drawBitmap(0, 0, boot_logo, 400, 243, GxEPD_BLACK);
  } while (display.nextPage());

  updateBootStatus("Wczytywanie konfiguracji...");
  loadConfig();

  // --- ODCZYT FLAGI AWARYJNEJ Z POPRZEDNIEJ SESJI ---
  prefs.begin("aprs", false);
  if (prefs.getBool("apFallback", false)) {
      prefs.putBool("apFallback", false); // Czyścimy flagę, by nie wpaść w pętlę przy kolejnym restarcie
      btFallbackTriggered = true;         // Wymuszamy środowisko do startu bezpośrednio w trybie awaryjnym
  }
  prefs.end();

  lastRefreshTime = millis(); 
  lastBeaconTime = millis(); 

  if (appConfig.currentMode == MODE_KISS_BT && !btFallbackTriggered) {
      // NORMALNY START BLUETOOTH
      WiFi.disconnect(true); 
      WiFi.mode(WIFI_OFF);   
      
      updateBootStatus("Inicjalizacja Bluetooth...");
      SerialBT.begin("ESP32_Node", true); 
      if(appConfig.btPin.length() > 0) {
         SerialBT.setPin(appConfig.btPin.c_str(), appConfig.btPin.length());
      }
      btConnectAttemptTime = millis();
      addToTerminal("Uruchomiono BT Master. Start odliczania...");
  } else {
      // START WIFI / AWARYJNY
      if (btFallbackTriggered) {
          // Jeżeli odpaliła się flaga fallbacka, całkowicie omijamy próby łączenia z domowym routerem
          // i zmuszamy ESP32 do włączenia własnej sieci AP, by ułatwić zmianę ustawień
          updateBootStatus("Brak polaczenia BT. Uruchamiam AP...");
          WiFi.mode(WIFI_AP);
          WiFi.softAP("APRS_DISPLAY_SETUP");
          addToTerminal("Tryb AP: 192.168.4.1");
          updateBootStatus("Brak TNC BT! AP: 192.168.4.1");
      } else {
          // Standardowe łączenie z WiFi (gdy tryb to nie BT)
          if (appConfig.wifiSSID.length() > 0) {
            updateBootStatus("Laczenie z siecia WiFi...");
            WiFi.mode(WIFI_STA);
            WiFi.begin(appConfig.wifiSSID.c_str(), appConfig.wifiPass.c_str());
            int retries = 0;
            while (WiFi.status() != WL_CONNECTED && retries < 20) { delay(500); retries++; }
          }

          if (WiFi.status() != WL_CONNECTED) {
            WiFi.softAP("APRS_DISPLAY_SETUP");
            addToTerminal("Tryb AP: 192.168.4.1");
            updateBootStatus("Brak WiFi. Tryb AP: 192.168.4.1");
          } else {
            addToTerminal("WiFi OK! IP: " + WiFi.localIP().toString());
            updateBootStatus("WiFi OK! IP: " + WiFi.localIP().toString());
          }
      }

      updateBootStatus("Uruchamianie serwera WWW...");
      setupWebEndpoints();
      server.begin();
  }

  if (appConfig.currentMode == MODE_KISS_UART) {
    VPSerial.begin(appConfig.uartBaud, SERIAL_8N1, VP_RX_PIN, VP_TX_PIN);
    addToTerminal("UART Gotowy (" + String(appConfig.uartBaud) + " bps)");
    updateBootStatus("UART Gotowy (" + String(appConfig.uartBaud) + " bps)");
  } 

  updateBootStatus("Uruchamianie systemu...");
  delay(1000); 

  lastPacket.sourceCall = "";
  lastPacket.payload = "Oczekuje na dane...";
  lastPacket.rawPacket = "Oczekuje na dane..."; 
  lastPacket.parsedText = "Oczekuje na dane...";
  lastPacket.comment = "Oczekuje na dane...";
  lastPacket.distance = -1;
  lastPacket.bearing = -1;
  newDataToDisplay = true; 
}

void loop() {
  if (appConfig.currentMode != MODE_KISS_BT || btFallbackTriggered) {
      server.handleClient();
  }
  
  handleNetwork();
  
  unsigned long currentMillis = millis();
  
  if (newDataToDisplay && (currentMillis - lastDisplayUpdate >= 3000)) {
    lastDisplayUpdate = currentMillis;
    newDataToDisplay = false;
    updateDisplay();
  }

  if (appConfig.refreshInterval > 0 && (currentMillis - lastRefreshTime >= (appConfig.refreshInterval * 60000UL))) {
    lastRefreshTime = currentMillis;
    forceFullRefresh = true;
    newDataToDisplay = true; 
    addToTerminal("> Automatyczne odświeżenie E-INK");
  }

  if (appConfig.beaconEnabled && appConfig.beaconInterval > 0) {
    if (currentMillis - lastBeaconTime >= (appConfig.beaconInterval * 60000UL)) {
      lastBeaconTime = currentMillis;
      transmitBeacon();
    }
  }

  if (appConfig.currentMode == MODE_KISS_UART) {
    while (VPSerial.available()) {
      uint8_t b = VPSerial.read();
      parseKISS(b); 
    }
  }

  // ZABEZPIECZENIE: Używaj SerialBT tylko jeśli nie działa tryb awaryjny
  if (appConfig.currentMode == MODE_KISS_BT && !btFallbackTriggered && SerialBT.connected()) {
    int maxReads = 128;
    while (SerialBT.available() && maxReads-- > 0) {
      uint8_t b = SerialBT.read();
      parseKISS(b);
    }
  }

  delay(10); 
}