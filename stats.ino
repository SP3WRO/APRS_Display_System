// --- STATYSTYKI: słyszane stacje, DX dnia ---

void resetDxIfNewDay() {
  struct tm t;
  if (!getLocalTimeNow(t)) return;
  if (dxDay != t.tm_yday) {
    if (dxDay >= 0) addToTerminal("> Nowy dzien - zeruje DX dnia");
    dxDay = t.tm_yday;
    dxCall = "";
    dxDistance = -1;
    dxBearing = -1;
    dxTime = "";
  }
}

void updateStats(const APRSPacket &pkt) {
  totalPackets++;
  if (pkt.sourceCall.length() == 0) return;

  unsigned long now = millis();
  // Ta sama stacja -> wolne miejsce -> najdawniej słyszana
  int slot = -1;
  for (int i = 0; i < MAX_HEARD && slot < 0; i++) {
    if (heard[i].call == pkt.sourceCall) slot = i;
  }
  for (int i = 0; i < MAX_HEARD && slot < 0; i++) {
    if (heard[i].call.length() == 0) slot = i;
  }
  if (slot < 0) {
    slot = 0;
    for (int i = 1; i < MAX_HEARD; i++) {
      if (now - heard[i].lastHeard > now - heard[slot].lastHeard) slot = i;
    }
  }

  HeardStation &h = heard[slot];
  if (h.call != pkt.sourceCall) {
    h = HeardStation();
    h.call = pkt.sourceCall;
  }
  h.lastHeard = now;
  h.packets++;
  if (pkt.radioModel.length() > 0) h.radio = pkt.radioModel;
  String text = pkt.comment;
  text.replace('\n', ' ');
  if (text.length() > 60) text = text.substring(0, 60);
  h.text = text;
  if (pkt.distance >= 0) {
    h.lat = pkt.lat;
    h.lon = pkt.lon;
    h.distance = pkt.distance;
    h.bearing = pkt.bearing;
    h.symbolTable = pkt.symbolTable;
    h.symbolCode = pkt.symbolCode;
  }

}

// Wywoływane dla każdej ramki z pozycją, także tej odrzuconej przez filtr odległości.
// Liczą się tylko stacje słyszane bezpośrednio (bez digi) i ich własna pozycja (bez obiektów).
void updateDx(const APRSPacket &pkt) {
  resetDxIfNewDay();
  if (pkt.distance < 0 || pkt.sourceCall.length() == 0) return;
  if (pkt.isObject || !heardDirect(pkt)) return;
  if (pkt.distance > dxDistance) {
    bool newStation = (pkt.sourceCall != dxCall);
    dxDistance = pkt.distance;
    dxBearing = pkt.bearing;
    dxCall = pkt.sourceCall;
    struct tm t;
    if (getLocalTimeNow(t)) {
      char buf[8];
      snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
      dxTime = buf;
    } else {
      dxTime = "";
    }
    addToTerminal("> Nowy DX: " + dxCall + " " + String(dxDistance, 1) + "km");
    // Ekran odświeżamy tylko przy zmianie stacji, nie przy każdym kroku tej samej
    if (newStation) newDataToDisplay = true;
  }
}

int countHeardWithin(unsigned long windowMs) {
  unsigned long now = millis();
  int n = 0;
  for (int i = 0; i < MAX_HEARD; i++) {
    if (heard[i].call.length() > 0 && now - heard[i].lastHeard < windowMs) n++;
  }
  return n;
}

String formatAge(unsigned long ms) {
  unsigned long s = ms / 1000;
  if (s < 60) return String(s) + "s";
  if (s < 3600) return String(s / 60) + "min";
  return String(s / 3600) + "h";
}
