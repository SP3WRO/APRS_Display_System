// --- WIADOMOŚCI APRS: odbiór, ACK, wysyłanie z ponawianiem ---

// Czy wiadomość jest zaadresowana do nas (znak stacji lub znak APRS-IS)
bool isAddressedToUs(const String &addressee) {
  String a = addressee;
  a.toUpperCase();
  String my = appConfig.callsign;
  my.toUpperCase();
  return a == my || a == effectiveIsCall();
}

void storeMessage(const MessageEntry &m) {
  for (int i = MAX_MESSAGES - 1; i > 0; i--) messages[i] = messages[i - 1];
  messages[0] = m;
}

void sendAck(const APRSPacket &pkt) {
  // ACK wysyłamy z tego znaku, do którego przyszła wiadomość
  String payload = ":" + padCall9(pkt.sourceCall) + ":ack" + pkt.msgId;
  sendAPRSPayload(pkt.msgAddressee, payload);
}

// Zwraca true, jeśli wiadomość ma też trafić na ekran jako zwykła ramka.
bool handleIncomingMessage(APRSPacket &pkt) {
  if (!isAddressedToUs(pkt.msgAddressee)) {
    addToTerminal("> UKRYTO SMS do " + pkt.msgAddressee + ": " + pkt.sourceCall);
    return false;
  }

  if (pkt.isAck || pkt.isReject) {
    if (outbox.active && outbox.id == pkt.msgId) {
      outbox.active = false;
      for (int i = 0; i < MAX_MESSAGES; i++) {
        if (messages[i].outgoing && messages[i].id == pkt.msgId) { messages[i].acked = pkt.isAck; break; }
      }
      addToTerminal(String("> MSG ") + (pkt.isAck ? "potwierdzona" : "odrzucona") + " przez " + pkt.sourceCall);
    }
    return false;
  }

  // Powtórka (nadawca nie dostał naszego ACK) - potwierdzamy jeszcze raz, ale nie pokazujemy
  if (pkt.msgId.length() > 0) {
    for (int i = 0; i < MAX_MESSAGES; i++) {
      if (!messages[i].outgoing && messages[i].from == pkt.sourceCall && messages[i].id == pkt.msgId &&
          millis() - messages[i].ms < 30UL * 60000UL) {
        if (appConfig.msgAck) sendAck(pkt);
        addToTerminal("> MSG powtorka od " + pkt.sourceCall + " (ponowny ACK)");
        return false;
      }
    }
  }

  MessageEntry m;
  m.from = pkt.sourceCall;
  m.to = pkt.msgAddressee;
  m.text = pkt.msgText;
  m.id = pkt.msgId;
  m.ms = millis();
  storeMessage(m);
  unreadMessages++;

  addToTerminal("> MSG od " + pkt.sourceCall + ": " + pkt.msgText);
  if (pkt.msgId.length() > 0 && appConfig.msgAck) sendAck(pkt);

  pkt.parsedText = "[SMS od " + pkt.sourceCall + "]\n" + pkt.msgText;
  pkt.comment = "[SMS] " + pkt.msgText;
  forceFullRefresh = true;
  return true;
}

String outboxPayload() {
  return ":" + padCall9(outbox.to) + ":" + outbox.text + "{" + outbox.id;
}

// Zwraca pusty String przy sukcesie albo opis błędu.
String queueOutgoingMessage(String to, String text) {
  to.trim(); to.toUpperCase();
  text.trim();
  text = toDisplayAscii(text);
  text.replace("|", ""); text.replace("~", ""); text.replace("{", "");
  if (to.length() < 3 || to.length() > 9) return "Niepoprawny znak adresata";
  if (text.length() == 0) return "Pusta wiadomosc";
  if (text.length() > 67) text = text.substring(0, 67);
  if (outbox.active) return "Poprzednia wiadomosc czeka na potwierdzenie";

  outbox.active = true;
  outbox.to = to;
  outbox.text = text;
  outbox.id = String(nextMsgId);
  nextMsgId = (nextMsgId % 999) + 1;
  outbox.tries = 0;
  outbox.lastTry = 0;

  MessageEntry m;
  m.from = appConfig.callsign;
  m.to = to;
  m.text = text;
  m.id = outbox.id;
  m.outgoing = true;
  m.ms = millis();
  storeMessage(m);
  return "";
}

void handleMessages() {
  if (!outbox.active) return;
  unsigned long now = millis();
  if (outbox.lastTry != 0 && now - outbox.lastTry < MSG_RETRY_MS) return;

  if (outbox.tries >= MSG_MAX_TRIES) {
    addToTerminal("> MSG do " + outbox.to + " bez potwierdzenia po " + String(MSG_MAX_TRIES) + " probach");
    outbox.active = false;
    return;
  }

  outbox.lastTry = now;
  if (sendAPRSPayload(appConfig.callsign, outboxPayload())) outbox.tries++;
}
