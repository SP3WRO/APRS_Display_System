// --- EKRAN E-INK ---

// Czcionki U8g2 (Helvetica, zwykła - bez pogrubienia): rysowane piksel po pikselu,
// więc ostre na e-papierze, i z polskimi literami (wersje *_te = Latin Extended).
// Hierarchię daje rozmiar, nie grubość.
#define FONT_BIG   u8g2_font_helvR24_te   // znak stacji, EMERGENCY, DX na statystykach
#define FONT_TITLE u8g2_font_helvR18_te   // paski tytułowe, dystans, model radia
#define FONT_CALL  u8g2_font_helvR14_te   // znaki stacji w wierszach listy
#define FONT_BODY  u8g2_font_helvR14_te   // główna treść ramki
#define FONT_LABEL u8g2_font_helvR12_te   // etykiety, stopka, historia
#define FONT_LIGHT u8g2_font_helvR12_te   // treść drugorzędna (linie na liście)

// U8g2 przy każdej zmianie czcionki wraca do trybu "z tłem" (rysuje prostokąt
// w kolorze tła pod każdą literą) - na czarnych paskach dawało to białe kwadraty.
void useFont(const uint8_t *font) {
  u8g2Fonts.setFont(font);
  u8g2Fonts.setFontMode(1);
}

// Bitmapa symbolu APRS: tabela '/' = podstawowa, '\' i nakładki (0-9, A-Z) = alternatywna.
// Zwraca NULL dla kodu spoza zakresu albo symbolu bez grafiki (logo firm).
const uint8_t *getSymbolBitmap(char table, char code) {
  if (code < '!' || code > '~') return NULL;
  const uint8_t *bmp = (table == '/') ? aprs_symbols_primary[code - '!'] : aprs_symbols_alternate[code - '!'];
  for (int i = 0; i < 128; i++) {
    if (pgm_read_byte(bmp + i) != 0) return bmp;
  }
  return NULL;
}

// Symbol ma sens tylko dla ramek z pozycją (i pogody bez pozycji) - status czy
// wiadomość nie niosą symbolu, a domyślne "/L" pokazywałoby ikonę komputera.
bool packetHasSymbol(const APRSPacket &p) {
  return p.distance >= 0 || p.isWX;
}

// Rysuje symbol APRS 32x32 w (x, y). onBlack = na czarnym pasku (dostaje białe tło).
// Nakładka (np. "L#" = digi LoRa) jest rysowana jako znak w ramce na środku ikony.
void drawAprsSymbol(int x, int y, char table, char code, bool onBlack) {
  if (onBlack) display.fillRoundRect(x - 3, y - 3, 38, 38, 5, GxEPD_WHITE);

  const uint8_t *bmp = getSymbolBitmap(table, code);
  if (bmp) {
    display.drawBitmap(x, y, bmp, 32, 32, GxEPD_BLACK);
  } else {
    // Brak grafiki: ramka ze znakiem symbolu
    display.drawRoundRect(x + 2, y + 2, 28, 28, 4, GxEPD_BLACK);
    useFont(FONT_TITLE);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    String c = String(code);
    u8g2Fonts.setCursor(x + 16 - textWidth(c) / 2, y + 25);
    u8g2Fonts.print(c);
  }

  bool overlay = (table >= '0' && table <= '9') || (table >= 'A' && table <= 'Z');
  if (overlay) {
    useFont(FONT_LABEL);
    String c = String(table);
    int w = textWidth(c) + 6;
    display.fillRect(x + 16 - w / 2, y + 8, w, 16, GxEPD_WHITE);
    display.drawRect(x + 16 - w / 2, y + 8, w, 16, GxEPD_BLACK);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.setCursor(x + 16 - textWidth(c) / 2 + 1, y + 21);
    u8g2Fonts.print(c);
  }
}

int textWidth(const String &s) {
  return u8g2Fonts.getUTF8Width(s.c_str());
}

// Długość prefiksu (w bajtach) cofnięta do granicy znaku UTF-8 - nie tniemy "ł" w połowie
unsigned int utf8Boundary(const String &s, unsigned int n) {
  while (n > 0 && n < s.length() && ((uint8_t)s.charAt(n) & 0xC0) == 0x80) n--;
  return n;
}

// Przycina tekst (aktualną czcionką) do zadanej szerokości w pikselach
String fitText(String s, int maxW) {
  if (textWidth(s) <= maxW) return s;
  while (s.length() > 1 && textWidth(s + "..") > maxW) {
    s = s.substring(0, utf8Boundary(s, s.length() - 1));
  }
  return s + "..";
}

void printRightAligned(const String &s, int rightX, int y) {
  u8g2Fonts.setCursor(rightX - textWidth(s), y);
  u8g2Fonts.print(s);
}

String dxShortText() {
  if (dxDistance < 0) return "";
  return String(dxDistance, 0) + "km " + getCompassDir(dxBearing);
}

String packetDisplayText(const APRSPacket &p) {
  String pld;
  if (p.isEmergency || p.isMessage) pld = p.parsedText;
  else if (appConfig.dataFormat == 1) pld = toDisplayText(p.rawPacket);
  else if (appConfig.dataFormat == 2) pld = p.isWX ? p.parsedText : p.comment;
  else pld = p.parsedText;
  if (pld.length() == 0) pld = "(brak danych)";
  pld.replace("\r", "");
  return pld;
}

// Zawijanie tekstu po słowach według szerokości w pikselach (aktualna czcionka).
// Zwraca Y następnej wolnej linii.
int drawWrappedText(String text, int x, int lineY, int maxY, int maxWidth, int lineHeight) {
  String currentLine = "";
  unsigned int i = 0;

  auto flushLine = [&]() {
    if (lineY < maxY) {
      u8g2Fonts.setCursor(x, lineY);
      u8g2Fonts.print(currentLine);
    }
    lineY += lineHeight;
    currentLine = "";
  };

  while (i < text.length() && lineY < maxY) {
    int nextSpace = text.indexOf(' ', i);
    int nextNewline = text.indexOf('\n', i);

    int wordEnd;
    bool isNewline = false;
    if (nextNewline != -1 && (nextSpace == -1 || nextNewline < nextSpace)) {
      wordEnd = nextNewline;
      isNewline = true;
    } else if (nextSpace != -1) {
      wordEnd = nextSpace;
    } else {
      wordEnd = text.length();
    }

    String word = text.substring(i, wordEnd);

    // Słowo dłuższe niż linia (np. surowa ramka) - tniemy po znakach
    while (word.length() > 1 && textWidth(word) > maxWidth && lineY < maxY) {
      if (currentLine.length() > 0) flushLine();
      unsigned int n = word.length() - 1;
      while (n > 1 && textWidth(word.substring(0, n)) > maxWidth) n--;
      n = utf8Boundary(word, n);
      if (n == 0) n = word.length();
      currentLine = word.substring(0, n);
      flushLine();
      word = word.substring(n);
    }

    String candidate = currentLine.length() > 0 ? currentLine + " " + word : word;
    if (textWidth(candidate) <= maxWidth) {
      currentLine = candidate;
    } else {
      flushLine();
      currentLine = word;
    }

    if (isNewline) flushLine();

    i = wordEnd + 1;
  }

  if (currentLine.length() > 0) flushLine();
  return lineY;
}

void drawEmergencyBar(int y) {
  display.fillRect(0, y, 400, 35, GxEPD_BLACK);
  useFont(FONT_BIG);
  u8g2Fonts.setForegroundColor(GxEPD_WHITE);
  u8g2Fonts.setCursor((400 - textWidth("EMERGENCY!")) / 2, y + 30);
  u8g2Fonts.print("EMERGENCY!");
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);
}

void drawClassicScreen() {
  display.fillRect(0, 0, 400, 50, GxEPD_BLACK);
  useFont(FONT_BIG);
  u8g2Fonts.setForegroundColor(GxEPD_WHITE);
  u8g2Fonts.setCursor(10, 35);

  String topCall = lastPacket.sourceCall.length() > 0 ? lastPacket.sourceCall : "OCZEKUJE...";
  if (lastPacket.isEmergency && lastPacket.distance >= 0) topCall += " " + String(lastPacket.distance, 0) + "km";
  u8g2Fonts.print(fitText(topCall, 335));

  if (lastPacket.sourceCall.length() > 0 && packetHasSymbol(lastPacket)) drawAprsSymbol(352, 9, lastPacket.symbolTable, lastPacket.symbolCode, true);
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);

  int cx = 75, cy = 125, r = 40;
  display.drawCircle(cx, cy, r, GxEPD_BLACK);
  display.drawCircle(cx, cy, r - 1, GxEPD_BLACK);
  display.drawLine(cx, cy - r, cx, cy + r, GxEPD_BLACK);
  display.drawLine(cx - r, cy, cx + r, cy, GxEPD_BLACK);

  useFont(FONT_LABEL);
  u8g2Fonts.setCursor(cx - textWidth("N") / 2, cy - r - 4); u8g2Fonts.print("N");
  u8g2Fonts.setCursor(cx - textWidth("S") / 2, cy + r + 14); u8g2Fonts.print("S");
  u8g2Fonts.setCursor(cx + r + 5, cy + 4); u8g2Fonts.print("E");
  u8g2Fonts.setCursor(cx - r - 5 - textWidth("W"), cy + 4); u8g2Fonts.print("W");

  if (lastPacket.distance > 0 && lastPacket.bearing >= 0) {
    float rad = lastPacket.bearing * M_PI / 180.0;
    int dotX = cx + (sin(rad) * (r - 8));
    int dotY = cy - (cos(rad) * (r - 8));
    display.fillCircle(dotX, dotY, 6, GxEPD_BLACK);
  }

  useFont(FONT_TITLE);
  if (lastPacket.distance >= 0 && !lastPacket.isEmergency) {
    String dStr = String(lastPacket.distance, 1) + " km";
    u8g2Fonts.setCursor(cx - textWidth(dStr) / 2, cy + r + 35);
    u8g2Fonts.print(dStr);
  }

  useFont(FONT_BODY);
  drawWrappedText(packetDisplayText(lastPacket), 160, 85, lastPacket.isEmergency ? 165 : 205, 400 - 160 - 4, 20);

  if (lastPacket.isEmergency) drawEmergencyBar(170);

  display.drawLine(0, 205, 400, 205, GxEPD_BLACK);
  display.drawLine(250, 205, 250, 250, GxEPD_BLACK);

  useFont(FONT_LABEL);
  int yPos = 225;
  for (int hist_i = 0; hist_i < 2; hist_i++) {
    if (history[hist_i].sourceCall.length() > 0) {
      u8g2Fonts.setCursor(5, yPos);
      String histLine = history[hist_i].sourceCall;
      if (history[hist_i].distance >= 0) histLine += " (" + String(history[hist_i].distance, 0) + "km)";
      u8g2Fonts.print(histLine);
      yPos += 22;
    }
  }

  // Prawy dolny róg: model radia + DX dnia
  const int boxX = 256, boxW = 400 - boxX - 4;
  if (lastPacket.radioModel.length() > 0) {
    useFont(FONT_TITLE);
    u8g2Fonts.setCursor(boxX, 228);
    u8g2Fonts.print(fitText(lastPacket.radioModel, boxW));
  }
  if (dxDistance >= 0) {
    useFont(FONT_LABEL);
    u8g2Fonts.setCursor(boxX, 249);
    u8g2Fonts.print(fitText("DX: " + dxCall, boxW));
    u8g2Fonts.setCursor(boxX, 269);
    u8g2Fonts.print(fitText(dxShortText(), boxW));
  }
}

void drawListScreen() {
  display.fillRect(0, 0, 400, 30, GxEPD_BLACK);
  useFont(FONT_TITLE);
  u8g2Fonts.setForegroundColor(GxEPD_WHITE);
  u8g2Fonts.setCursor(6, 23);

  if (lastPacket.isEmergency) {
    u8g2Fonts.print("ALARM RATUNKOWY!");
  } else if (dxDistance >= 0) {
    u8g2Fonts.print("STACJE");
    useFont(FONT_LABEL);
    printRightAligned(fitText("DX " + dxCall + " " + dxShortText(), 290), 394, 21);
  } else {
    u8g2Fonts.print("LISTA STACJI APRS");
  }
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);

  unsigned long now = millis();
  // Wiersze o stałej wysokości 40 px (6 mieści się między paskiem tytułu a stopką):
  //   +2..+16  znak stacji (Helvetica 14: litery 14 px nad linią bazową)
  //   +22..+38 treść (Helvetica 12 z ogonkami liter)
  //   +4..+36  ikona 32x32
  //   +39      linia oddzielająca
  const int ROW_H = 40;
  int rowTop = 33;
  int rowsBottom = lastPacket.isEmergency ? 233 : 274;   // pasek EMERGENCY / stopka

  for (int i = -1; i < MAX_HISTORY; i++) {
    APRSPacket *p = (i == -1) ? &lastPacket : &history[i];
    if (p->sourceCall.length() == 0) continue;
    if (rowTop + ROW_H - 1 > rowsBottom) break;
    int yPos = rowTop + 16;

    bool icon = packetHasSymbol(*p);
    if (icon) drawAprsSymbol(4, rowTop + 4, p->symbolTable, p->symbolCode, false);
    int textX = icon ? 42 : 5;
    useFont(FONT_CALL);
    u8g2Fonts.setCursor(textX, yPos);
    String header = p->sourceCall;

    if (p->distance >= 0) {
      header += " [" + String(p->distance, 0) + "km]";
      if (p->bearing >= 0) header += " [" + getCompassDir(p->bearing) + "]";
    }
    if (p->radioModel.length() > 0) header += " [" + p->radioModel + "]";
    if (p->isEmergency) header = "! " + header;

    // Po prawej: jak dawno słyszana (kolejne odświeżenia ekranu aktualizują wiek)
    unsigned long ageMs = now - p->rxMillis;
    String age = ageMs < 60000UL ? "teraz" : formatAge(ageMs);
    int ageW = textWidth(age);
    u8g2Fonts.print(fitText(header, 390 - ageW - 8 - textX));
    printRightAligned(age, 395, yPos);

    useFont(FONT_LIGHT);
    u8g2Fonts.setCursor(icon ? textX : 15, rowTop + 34);

    String pld = packetDisplayText(*p);
    pld.replace('\n', ' ');
    u8g2Fonts.print(fitText(pld, 390 - (icon ? textX : 15)));

    display.drawLine(5, rowTop + ROW_H - 1, 395, rowTop + ROW_H - 1, GxEPD_BLACK);
    rowTop += ROW_H;
  }

  if (lastPacket.isEmergency) drawEmergencyBar(235);
}

void drawStatsScreen() {
  display.fillRect(0, 0, 400, 30, GxEPD_BLACK);
  useFont(FONT_TITLE);
  u8g2Fonts.setForegroundColor(GxEPD_WHITE);
  u8g2Fonts.setCursor(6, 23);
  u8g2Fonts.print(lastPacket.isEmergency ? "ALARM RATUNKOWY!" : "STATYSTYKI APRS");

  struct tm t;
  if (getLocalTimeNow(t)) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    printRightAligned(String(buf), 394, 23);
  }
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);

  // DX dnia
  useFont(FONT_LABEL);
  u8g2Fonts.setCursor(5, 52);
  String dxLabel = dxDay >= 0 ? "DX dnia" : "DX od uruchomienia";
  if (dxTime.length() > 0) dxLabel += " (o " + dxTime + ")";
  u8g2Fonts.print(dxLabel + ":");
  if (dxDistance >= 0) {
    useFont(FONT_TITLE);
    String dist = dxShortText();
    int distW = textWidth(dist);
    printRightAligned(dist, 395, 88);
    useFont(FONT_BIG);
    u8g2Fonts.setCursor(5, 88);
    u8g2Fonts.print(fitText(dxCall, 395 - distW - 15));
  } else {
    useFont(FONT_BIG);
    u8g2Fonts.setCursor(5, 88);
    u8g2Fonts.print("---");
  }

  display.drawLine(0, 100, 400, 100, GxEPD_BLACK);

  // Liczniki
  useFont(FONT_LABEL);
  u8g2Fonts.setCursor(5, 122);
  u8g2Fonts.print("Stacje 1h: " + String(countHeardWithin(3600000UL)) + "   24h: " + String(countHeardWithin(86400000UL)));
  u8g2Fonts.setCursor(5, 142);
  u8g2Fonts.print("Ramki: " + String(totalPackets) + "   Uptime: " + formatAge(millis()));

  display.drawLine(0, 152, 400, 152, GxEPD_BLACK);

  // Ostatnia wiadomość albo ostatnia stacja
  useFont(FONT_LABEL);
  if (messages[0].text.length() > 0) {
    String head = messages[0].outgoing ? ("Do " + messages[0].to) : ("Od " + messages[0].from);
    head += " (" + formatAge(millis() - messages[0].ms) + " temu)";
    if (messages[0].outgoing) head += messages[0].acked ? " OK" : " ...";
    u8g2Fonts.setCursor(5, 172);
    u8g2Fonts.print(fitText(head, 390));
    useFont(FONT_LIGHT);
    drawWrappedText(messages[0].text, 5, 194, 270, 390, 20);
  } else if (lastPacket.sourceCall.length() > 0) {
    u8g2Fonts.setCursor(5, 172);
    u8g2Fonts.print("Ostatnio: " + lastPacket.sourceCall);
    useFont(FONT_LIGHT);
    String txt = packetDisplayText(lastPacket);
    txt.replace('\n', ' ');
    drawWrappedText(txt, 5, 194, 270, 390, 20);
  }

  if (lastPacket.isEmergency) drawEmergencyBar(235);
}

void drawFooter() {
  display.fillRect(0, 275, 400, 25, GxEPD_BLACK);
  useFont(FONT_LABEL);
  u8g2Fonts.setForegroundColor(GxEPD_WHITE);
  int textX = 5;

  // Nieprzeczytane wiadomości: odwrócona plakietka, widoczna z daleka
  if (unreadMessages > 0) {
    String badge = "MSG " + String(unreadMessages);
    int bw = textWidth(badge) + 10;
    display.fillRoundRect(3, 278, bw, 19, 4, GxEPD_WHITE);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.setCursor(8, 293);
    u8g2Fonts.print(badge);
    u8g2Fonts.setForegroundColor(GxEPD_WHITE);
    textX = bw + 9;
  }
  u8g2Fonts.setCursor(textX, 293);

  String modeStr = "";
  String ipStr = "Brak";
  String netStr = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : (apActive ? "AP 192.168.4.1" : "Brak WiFi");

  if (appConfig.currentMode == MODE_KISS_UART) {
    modeStr = "VP-DIGI (" + String(appConfig.uartBaud) + ")";
    ipStr = netStr;
  } else if (appConfig.currentMode == MODE_KISS_TCP) {
    modeStr = "KISS TCP";
    ipStr = netStr;
  } else if (appConfig.currentMode == MODE_APRS_IS) {
    modeStr = "APRS-IS";
    ipStr = netStr;
  } else if (appConfig.currentMode == MODE_KISS_BT) {
    modeStr = "Bluetooth SPP";
    if (btFallbackTriggered) ipStr = "AP: 192.168.4.1";
    else ipStr = SerialBT.connected() ? "Połączono" : "Szukam HC-05...";
  }

  u8g2Fonts.print(fitText(appConfig.callsign + " | " + modeStr + " | " + ipStr, 395 - textX));
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);
}

void updateDisplay() {
  if (forceFullRefresh) { display.clearScreen(); display.setFullWindow(); forceFullRefresh = false; }
  else display.setPartialWindow(0, 0, 400, 300);

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    if (appConfig.displayMode == DISPLAY_LIST) drawListScreen();
    else if (appConfig.displayMode == DISPLAY_STATS) drawStatsScreen();
    else drawClassicScreen();
    drawFooter();
  } while (display.nextPage());
}

// Postęp startu w procentach; -1 = urządzenie już działa (bez paska)
int bootProgress = 0;

void setBootProgress(int percent, String text) {
  bootProgress = percent;
  updateBootStatus(text);
}

// Dolna część ekranu startowego: komunikat, pasek ładowania i wersja
void updateBootStatus(String text) {
  display.setPartialWindow(0, 243, 400, 57);
  display.firstPage();
  do {
    display.fillRect(0, 243, 400, 57, GxEPD_WHITE);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    useFont(FONT_LABEL);

    if (bootProgress < 0) {
      u8g2Fonts.setCursor(10, 276);
      u8g2Fonts.print(fitText(text, 380));
      continue;
    }

    String ver = "v" FW_VERSION;
    printRightAligned(ver, 392, 262);
    u8g2Fonts.setCursor(10, 262);
    u8g2Fonts.print(fitText(text, 380 - textWidth(ver) - 10));

    // Pasek: ramka 2 px + wypełnienie
    const int bx = 10, by = 272, bw = 380, bh = 16;
    display.drawRoundRect(bx, by, bw, bh, 4, GxEPD_BLACK);
    display.drawRoundRect(bx + 1, by + 1, bw - 2, bh - 2, 3, GxEPD_BLACK);
    int fill = (bw - 8) * constrain(bootProgress, 0, 100) / 100;
    if (fill > 0) display.fillRect(bx + 4, by + 4, fill, bh - 8, GxEPD_BLACK);
  } while (display.nextPage());
}
