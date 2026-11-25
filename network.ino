// --- ODBIÓR: KISS, APRS-IS, FILTRY, HISTORIA ---

void addCallsignToAX25(uint8_t *frame, int &len, String call, bool isLast) {
  int ssid = 0;
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

bool canTransmit() {
  if (appConfig.currentMode == MODE_KISS_UART) return true;
  if (appConfig.currentMode == MODE_KISS_TCP) return tcpClient.connected();
  if (appConfig.currentMode == MODE_KISS_BT) return isBtMode() && SerialBT.connected();
  if (appConfig.currentMode == MODE_APRS_IS) return tcpClient.connected() && isisLoggedIn;
  return false;
}

void sendKISSFrame(String source, String path, String payload) {
  uint8_t frame[340];
  int len = 0;

  // Rozbijamy ścieżkę "WIDE1-1,WIDE2-1" na maks. 8 digipeaterów
  String digis[8];
  int digiCount = 0;
  path.trim();
  while (path.length() > 0 && digiCount < 8) {
    int comma = path.indexOf(',');
    String d = (comma >= 0) ? path.substring(0, comma) : path;
    d.trim();
    if (d.length() > 0) digis[digiCount++] = d;
    path = (comma >= 0) ? path.substring(comma + 1) : "";
  }

  addCallsignToAX25(frame, len, "APZESP", false);
  addCallsignToAX25(frame, len, source, digiCount == 0);
  for (int i = 0; i < digiCount; i++) addCallsignToAX25(frame, len, digis[i], i == digiCount - 1);

  frame[len++] = 0x03;
  frame[len++] = 0xF0;

  for (unsigned int i = 0; i < payload.length() && len < (int)sizeof(frame); i++) {
    frame[len++] = payload.charAt(i);
  }

  auto writeByte = [&](uint8_t b) {
    if (appConfig.currentMode == MODE_KISS_UART) VPSerial.write(b);
    else if (appConfig.currentMode == MODE_KISS_TCP && tcpClient.connected()) tcpClient.write(b);
    else if (isBtMode() && SerialBT.connected()) SerialBT.write(b);
  };

  auto writeKISS = [&](uint8_t b) {
    if (b == FEND) { writeByte(FESC); writeByte(TFEND); }
    else if (b == FESC) { writeByte(FESC); writeByte(TFESC); }
    else writeByte(b);
  };

  writeByte(FEND); writeKISS(0x00);
  for (int i = 0; i < len; i++) writeKISS(frame[i]);
  writeByte(FEND);

  addToTerminal("> TX KISS: " + source + ": " + payload);
}

// Wysyła payload APRS przez aktualne złącze (RF przez KISS albo APRS-IS)
bool sendAPRSPayload(String source, String payload) {
  if (!canTransmit()) {
    addToTerminal("> TX pominiety (brak polaczenia)");
    return false;
  }
  if (appConfig.currentMode == MODE_APRS_IS) {
    tcpClient.print(source + ">APZESP,TCPIP*:" + payload + "\r\n");
    addToTerminal("> TX IS: " + source + ": " + payload);
  } else {
    sendKISSFrame(source, appConfig.beaconPath, payload);
  }
  return true;
}

void transmitBeacon() {
  if (!appConfig.beaconEnabled) return;

  char symT = appConfig.beaconSymbol.length() > 0 ? appConfig.beaconSymbol.charAt(0) : '/';
  char symC = appConfig.beaconSymbol.length() > 1 ? appConfig.beaconSymbol.charAt(1) : '-';

  String payload = "=" + formatAprsLat(appConfig.myLat) + symT + formatAprsLon(appConfig.myLon) + symC + appConfig.beaconComment;
  sendAPRSPayload(appConfig.callsign, payload);
}

bool isDuplicatePacket(const APRSPacket &pkt) {
  unsigned long now = millis();
  for (int i = 0; i < MAX_SEEN; i++) {
    if (seenStations[i].call == pkt.sourceCall && seenStations[i].payload == pkt.payload) {
      bool dup = (now - seenStations[i].lastSeen < 10000);
      seenStations[i].lastSeen = now;
      return dup;
    }
  }
  seenStations[seenIndex].call = pkt.sourceCall;
  seenStations[seenIndex].payload = pkt.payload;
  seenStations[seenIndex].lastSeen = now;
  seenIndex = (seenIndex + 1) % MAX_SEEN;
  return false;
}

void pushHistory(const APRSPacket &pkt) {
  for (int i = MAX_HISTORY - 1; i > 0; i--) history[i] = history[i - 1];
  history[0] = pkt;
}

void processParsedPacket(APRSPacket &pkt) {
  parseAPRSPacket(pkt, appConfig.myLat, appConfig.myLon);
  pkt.rxMillis = millis();

  if (pkt.isMessage) {
    // false = wiadomość obsłużona w całości (ack, nie do nas, powtórka)
    if (!handleIncomingMessage(pkt)) return;
  } else if (appConfig.antiDuplicate && isDuplicatePacket(pkt)) {
    addToTerminal("> DUPLIKAT: " + pkt.sourceCall);
    return;
  }

  // DX liczymy przed filtrami - filtr odległości z natury odrzuca najdalsze stacje
  updateDx(pkt);

  // EMERGENCY omija wszystkie filtry
  if (!pkt.isEmergency) {
    if (appConfig.maxDistKm > 0 && pkt.distance > appConfig.maxDistKm) {
      addToTerminal("> ODRZUCONO (" + String(pkt.distance, 0) + "km): " + pkt.sourceCall);
      return;
    }
    if (appConfig.hideNonPositional && pkt.distance < 0 && !pkt.isMessage) {
      addToTerminal("> UKRYTO (Brak poz): " + pkt.sourceCall);
      return;
    }
  }

  updateStats(pkt);

  if (emergencyActive && !pkt.isEmergency) {
    pushHistory(pkt);
    addToTerminal("> Tlo (Alarm trw): " + pkt.sourceCall);
  } else {
    if (lastPacket.sourceCall.length() > 0) pushHistory(lastPacket);
    lastPacket = pkt;
    if (pkt.isEmergency) {
      emergencyActive = true;
      emergencyStart = millis();
      forceFullRefresh = true;
      addToTerminal("> ALARM EMERGENCY!");
    }
  }
  newDataToDisplay = true;
}

void processAX25(int length) {
  // Bajt komendy KISS: górne 4 bity = port, dolne = typ (0 = dane)
  if (length < 17 || (kissBuffer[0] & 0x0F) != 0x00) return;

  APRSPacket pkt;
  if (!parseAX25Frame(kissBuffer + 1, length - 1, pkt)) return;

  addToTerminal("RX KISS: " + toDisplayText(pkt.rawPacket));
  processParsedPacket(pkt);
}

void parseKISS(uint8_t byte) {
  if (byte == FEND) {
    if (kissIndex > 0 && !kissOverflow) processAX25(kissIndex);
    kissIndex = 0; kissEscaped = false; kissOverflow = false;
    return;
  }
  if (kissEscaped) {
    if (byte == TFEND) byte = FEND;
    if (byte == TFESC) byte = FESC;
    kissEscaped = false;
  } else if (byte == FESC) {
    kissEscaped = true; return;
  }
  if (kissIndex < KISS_BUFFER_SIZE) kissBuffer[kissIndex++] = byte;
  else kissOverflow = true;   // uszkodzona/za długa ramka - odrzucamy całą
}

void parseAPRSIS(String line) {
  addToTerminal("IS: " + toDisplayText(line));
  if (line.indexOf("logresp") >= 0) {
    isisLoggedIn = true;
    if (line.indexOf("unverified") >= 0) addToTerminal("UWAGA: Haslo IS odrzucone! (tylko odbior)");
    else addToTerminal("APRS-IS Autoryzacja OK!");
  }

  APRSPacket pkt;
  if (parseAPRSISLine(line, pkt)) processParsedPacket(pkt);
}

String buildAprsIsFilter() {
  String f = appConfig.isFilter;
  f.trim();
  if (f.length() > 0) return f;
  // Bez filtra port 14580 nie przysyła ruchu, więc przy "0 = wył" bierzemy 100 km
  int radius = appConfig.maxDistKm > 0 ? appConfig.maxDistKm : 100;
  return "r/" + String(appConfig.myLat, 4) + "/" + String(appConfig.myLon, 4) + "/" + String(radius);
}

// --- WIFI: ponawianie po awarii routera, wyłączanie AP ---
void handleWifi(unsigned long now) {
  static unsigned long lastWifiRetry = 0;
  static unsigned long wifiOkSince = 0;

  if (btFallbackTriggered) {
    // Po 10 min w trybie awaryjnym bez nikogo w panelu - restart i kolejna próba BT
    if (now > BT_FALLBACK_RETRY_MS && WiFi.softAPgetStationNum() == 0) {
      addToTerminal("BT fallback: ponowna proba BT...");
      delay(200);
      ESP.restart();
    }
    return;
  }

  if (appConfig.wifiSSID.length() == 0) return;

  if (WiFi.status() != WL_CONNECTED) {
    wifiOkSince = 0;
    // Nie przełączamy kanału, gdy ktoś właśnie konfiguruje urządzenie przez AP
    if (now - lastWifiRetry > WIFI_RETRY_MS && (!apActive || WiFi.softAPgetStationNum() == 0)) {
      lastWifiRetry = now;
      addToTerminal("WiFi: ponawiam laczenie z " + appConfig.wifiSSID);
      if (!(WiFi.getMode() & WIFI_STA)) wifiModeLowPeak(apActive ? WIFI_AP_STA : WIFI_STA);
      WiFi.begin(appConfig.wifiSSID.c_str(), appConfig.wifiPass.c_str());
    }
  } else if (apActive) {
    if (wifiOkSince == 0) {
      wifiOkSince = now;
      addToTerminal("WiFi OK! IP: " + WiFi.localIP().toString());
      newDataToDisplay = true;
    }
    if (now - wifiOkSince > AP_SHUTDOWN_MS && WiFi.softAPgetStationNum() == 0) {
      WiFi.softAPdisconnect(true);
      wifiModeLowPeak(WIFI_STA);
      apActive = false;
      addToTerminal("AP wylaczony - dziala siec " + appConfig.wifiSSID);
      if (lastPacket.sourceCall.length() == 0) {
        lastPacket.parsedText = lastPacket.comment = lastPacket.rawPacket = "Oczekuję na dane...";
      }
      newDataToDisplay = true;
    }
  }
}

void handleBluetooth(unsigned long currentMillis) {
  if (!SerialBT.connected()) {
    String macStr = appConfig.btAddress;
    macStr.replace(":", "");
    macStr.replace("-", "");
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
      updateBootStatus("BT: Łączenie... (" + String(btFailedAttempts) + "/3)");

      for (int i = 0; i < 6; i++) {
        mac[i] = strtol(macStr.substring(i * 2, i * 2 + 2).c_str(), NULL, 16);
      }

      if (SerialBT.connect(mac)) {
        addToTerminal("BT: Polaczono z HC-05!");
        updateBootStatus("BT: Połączono!");
        forceFullRefresh = true;
        newDataToDisplay = true;
        btConnectAttemptTime = millis();
        btFailedAttempts = 0;
      } else {
        addToTerminal("BT: Blad polaczenia.");
        delay(100);
        SerialBT.disconnect(); // Oczyszczenie stanu maszyny BT
      }
      lastBtRetry = millis();
    }
  } else {
    btConnectAttemptTime = currentMillis;
    btFailedAttempts = 0; // Gdy zrywa stabilne połączenie, przyznajemy nowe 3 szanse
  }
}

void handleNetwork() {
  unsigned long currentMillis = millis();

  if (isBtMode()) {
    handleBluetooth(currentMillis);
    return;
  }

  handleWifi(currentMillis);

  bool tcpMode = (appConfig.currentMode == MODE_KISS_TCP || appConfig.currentMode == MODE_APRS_IS);
  if (!tcpMode) return;

  // Zerwane "po cichu" połączenie z APRS-IS: serwer milczy dłużej niż zwykle
  if (appConfig.currentMode == MODE_APRS_IS && tcpClient.connected() && currentMillis - lastTcpRx > APRSIS_TIMEOUT_MS) {
    addToTerminal("APRS-IS: brak danych " + String(APRSIS_TIMEOUT_MS / 1000) + "s - nowe polaczenie");
    tcpClient.stop();
  }

  if (WiFi.status() == WL_CONNECTED && !tcpClient.connected()) {
    if (currentMillis - lastTcpRetry > 5000 || lastTcpRetry == 0) {
      lastTcpRetry = currentMillis;
      isisLoggedIn = false;

      addToTerminal("Laczenie z serwerem TCP...");
      String host = (appConfig.currentMode == MODE_KISS_TCP) ? appConfig.kissIP : appConfig.isServer;
      int port = (appConfig.currentMode == MODE_KISS_TCP) ? appConfig.kissPort : appConfig.isPort;

      if (tcpClient.connect(host.c_str(), port)) {
        lastTcpRx = millis();
        addToTerminal("Polaczono z " + host);
        if (appConfig.currentMode == MODE_APRS_IS) {
          String login = "user " + effectiveIsCall() + " pass " + effectiveIsPass() + " vers APRSDisplay " FW_VERSION " filter " + buildAprsIsFilter() + "\r\n";
          tcpClient.print(login);
          addToTerminal("Przeslano autoryzacje (filtr: " + buildAprsIsFilter() + ")");
        } else {
          isisLoggedIn = true;
        }
      } else {
        addToTerminal("Odrzucono. Ponawiam...");
      }
    }
  }

  if (tcpClient.connected()) {
    static String tcpBuffer = "";
    while (tcpClient.available()) {
      char c = tcpClient.read();
      lastTcpRx = millis();
      if (appConfig.currentMode == MODE_KISS_TCP) {
        parseKISS(c);
      } else if (c == '\n') {
        tcpBuffer.trim();
        if (tcpBuffer.length() > 0) parseAPRSIS(tcpBuffer);
        tcpBuffer = "";
      } else if (c != '\r' && tcpBuffer.length() < 512) {
        tcpBuffer += c;
      }
    }
  }
}
