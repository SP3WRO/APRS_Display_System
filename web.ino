// --- PANEL WWW, MAPA, API JSON ---

String htmlEscape(const String &in) {
  String out;
  out.reserve(in.length() + 8);
  for (unsigned int i = 0; i < in.length(); i++) {
    char c = in.charAt(i);
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else if (c == '\'') out += "&#39;";
    else out += c;
  }
  return out;
}

String jsonEscape(const String &in) {
  String out = "\"";
  for (unsigned int i = 0; i < in.length(); i++) {
    char c = in.charAt(i);
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if ((unsigned char)c < 32) out += ' ';
    else out += c;
  }
  return out + "\"";
}

// Panel chroniony hasłem (użytkownik: admin), jeśli zostało ustawione
bool checkAuth() {
  if (appConfig.webPass.length() == 0) return true;
  if (server.authenticate("admin", appConfig.webPass.c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, "APRS Display", "Wymagane haslo panelu");
  return false;
}

String optionTag(int value, int current, const char *label) {
  return "<option value=\"" + String(value) + "\"" + (value == current ? " selected" : "") + ">" + label + "</option>";
}

String textInputEx(const char *label, const char *name, const String &value, const char *extra) {
  return String(label) + " <input type=\"text\" name=\"" + name + "\" value=\"" + htmlEscape(value) + "\" " + extra + ">";
}

String textInput(const char *label, const char *name, const String &value) {
  return textInputEx(label, name, value, "");
}

// Pole hasła nigdy nie zwraca zapisanej wartości - puste = bez zmian
String secretInput(const char *label, const char *name, bool isSet) {
  return String(label) + " <input type=\"password\" name=\"" + name + "\" autocomplete=\"new-password\" placeholder=\"" +
         (isSet ? "(ustawione - zostaw puste, by nie zmieniać)" : "(nie ustawiono)") + "\">";
}

const char PAGE_HEAD[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>APRS Display</title>
<style>
  body { font-family: Arial; padding: 10px; background: #f0f2f5; max-width: 900px; margin: auto; }
  .box { background: #fff; padding: 15px; border-radius: 8px; margin-bottom: 15px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
  input[type=text], input[type=password], input[type=number], select { width: 100%; padding: 8px; margin: 5px 0 15px 0; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; }
  textarea { width: 100%; height: 200px; background: #000; color: #0f0; font-family: monospace; padding: 10px; box-sizing: border-box; }
  .btn { background: #007BFF; color: #fff; padding: 10px; border: none; width: 100%; border-radius: 4px; font-size: 16px; cursor: pointer; }
  h3 { margin-top: 0; }
  nav a { margin-right: 15px; font-weight: bold; }
  .hint { color: #666; font-size: 13px; margin-top: -10px; margin-bottom: 12px; }
  .stats { display: flex; gap: 10px; flex-wrap: wrap; }
  .stat { flex: 1; min-width: 120px; background: #eaf4ff; border-radius: 6px; padding: 10px; }
  .stat b { display: block; font-size: 22px; }
  .msg { border-bottom: 1px solid #eee; padding: 6px 0; }
  .out { color: #0056b3; }
  #dform_msg { color: #d9534f; font-weight: bold; margin-top: 5px; font-size: 13px; }
</style>
)rawliteral";

const char PAGE_SCRIPT[] PROGMEM = R"rawliteral(<script>
  function toggle() {
    var m = document.getElementById('mode').value;
    document.getElementById('m_uart').style.display = (m==0) ? 'block' : 'none';
    document.getElementById('m_tcp').style.display  = (m==1) ? 'block' : 'none';
    document.getElementById('m_is').style.display   = (m==2) ? 'block' : 'none';
    document.getElementById('m_bt').style.display   = (m==3) ? 'block' : 'none';
    var dmode = document.getElementById('dmode').value;
    var dform = document.getElementById('dform');
    var dformMsg = document.getElementById('dform_msg');
    if (dmode == '1') { dform.value = '2'; dform.disabled = true; dformMsg.style.display = 'block'; }
    else { dform.disabled = false; dformMsg.style.display = 'none'; }
  }
  function esc(s) { var d = document.createElement('div'); d.textContent = s; return d.innerHTML; }
  // Zmiana trybu ekranu działa od razu - bez przycisku Zapisz i bez restartu
  function liveDisplay() {
    var info = document.getElementById('dmode_res');
    var body = new URLSearchParams({ dmode: document.getElementById('dmode').value, dform: document.getElementById('dform').value });
    info.textContent = 'Zmieniam...';
    fetch('/display', { method: 'POST', body: body }).then(r => r.json()).then(j => {
      info.textContent = j.ok ? '✓ Ekran przełączony' : 'Błąd';
    }).catch(() => { info.textContent = 'Błąd połączenia'; });
  }
  function refresh() {
    fetch('/terminal').then(r => r.text()).then(t => {
      var ta = document.getElementById('term');
      var atBottom = ta.scrollHeight - ta.clientHeight <= ta.scrollTop + 1;
      ta.value = t;
      if (atBottom) ta.scrollTop = ta.scrollHeight;
    });
    fetch('/api/status' + (document.visibilityState === 'visible' ? '?seen=1' : '')).then(r => r.json()).then(s => {
      document.getElementById('st_dx').innerHTML = s.dx.call ? esc(s.dx.call) + '<br><small>' + s.dx.km.toFixed(1) + ' km</small>' : '---';
      document.getElementById('st_1h').textContent = s.heard1h;
      document.getElementById('st_24h').textContent = s.heard24h;
      document.getElementById('st_pk').textContent = s.packets;
      var h = '';
      s.messages.forEach(m => {
        h += '<div class="msg' + (m.out ? ' out' : '') + '"><b>' + (m.out ? '&rarr; ' + esc(m.to) : esc(m.from)) + '</b> <small>(' + esc(m.age) + ' temu' +
             (m.out ? (m.acked ? ', potwierdzona' : ', bez ACK') : '') + ')</small><br>' + esc(m.text) + '</div>';
      });
      document.getElementById('msgs').innerHTML = h || '<i>Brak wiadomości</i>';
    });
  }
  function sendMsg(e) {
    e.preventDefault();
    var f = new FormData(document.getElementById('sendform'));
    fetch('/send', { method: 'POST', body: new URLSearchParams(f) }).then(r => r.json()).then(j => {
      document.getElementById('sendres').textContent = j.ok ? 'Wysłano - czekam na potwierdzenie' : j.error;
      if (j.ok) document.getElementById('msgtext').value = '';
      refresh();
    });
  }
  setInterval(refresh, 3000);
  window.onload = function() { toggle(); refresh(); };
</script>
)rawliteral";

const char MAP_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>APRS Display - mapa</title>
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css">
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
<style>html,body,#map{height:100%;margin:0;font-family:Arial} #bar{position:absolute;top:10px;right:10px;z-index:1000;background:#fff;padding:6px 10px;border-radius:6px;box-shadow:0 2px 4px rgba(0,0,0,.3)}</style>
</head><body><div id="map"></div><div id="bar"><a href="/">&larr; Panel</a> | <span id="cnt">...</span></div>
<script>
  if (typeof L === 'undefined') {
    document.getElementById('map').innerHTML = '<p style="padding:20px">Mapa wymaga dostępu do internetu w przeglądarce (biblioteka Leaflet i kafelki OpenStreetMap).</p>';
  } else {
    var map = L.map('map'), layer = L.layerGroup().addTo(map), first = true;
    L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', { maxZoom: 18, attribution: '&copy; OpenStreetMap' }).addTo(map);
    function esc(s) { var d = document.createElement('div'); d.textContent = s; return d.innerHTML; }
    function load() {
      fetch('/api/stations').then(r => r.json()).then(d => {
        layer.clearLayers();
        var me = L.circleMarker([d.me.lat, d.me.lon], { radius: 8, color: '#d9534f', fillOpacity: 0.8 }).bindPopup('<b>' + esc(d.me.call) + '</b><br>Moja stacja');
        layer.addLayer(me);
        if (d.range > 0) layer.addLayer(L.circle([d.me.lat, d.me.lon], { radius: d.range * 1000, color: '#999', fill: false, dashArray: '4' }));
        var pts = [[d.me.lat, d.me.lon]];
        d.stations.forEach(s => {
          var c = s.age < 600 ? '#007BFF' : (s.age < 3600 ? '#5cb85c' : '#999');
          var m = L.circleMarker([s.lat, s.lon], { radius: 6, color: c, fillOpacity: 0.7 });
          m.bindPopup('<b>' + esc(s.call) + '</b>' + (s.radio ? ' [' + esc(s.radio) + ']' : '') + '<br>' + s.km.toFixed(1) + ' km, ' + Math.round(s.brg) + '&deg;<br>' +
                      esc(s.text) + '<br><small>' + Math.round(s.age / 60) + ' min temu, ramek: ' + s.packets + '</small>');
          m.bindTooltip(esc(s.call), { permanent: true, direction: 'right', offset: [6, 0] });
          layer.addLayer(m);
          pts.push([s.lat, s.lon]);
        });
        document.getElementById('cnt').textContent = d.stations.length + ' stacji';
        if (first) { if (pts.length > 1) map.fitBounds(pts, { padding: [30, 30] }); else map.setView(pts[0], 10); first = false; }
      });
    }
    load(); setInterval(load, 15000);
  }
</script></body></html>)rawliteral";

void handleRoot() {
  if (!checkAuth()) return;
  unreadMessages = 0;
  newDataToDisplay = true;

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent_P(PAGE_HEAD);
  server.sendContent_P(PAGE_SCRIPT);

  String chunk = "</head><body><h2>APRS Display System " FW_VERSION "</h2><nav><a href=\"/\">Panel</a><a href=\"/map\">Mapa stacji</a></nav><br>";
  chunk += "<div class=\"box\"><h3>Statystyki</h3><div class=\"stats\">";
  chunk += "<div class=\"stat\">DX dnia<b id=\"st_dx\">-</b></div>";
  chunk += "<div class=\"stat\">Stacje (1h)<b id=\"st_1h\">-</b></div>";
  chunk += "<div class=\"stat\">Stacje (24h)<b id=\"st_24h\">-</b></div>";
  chunk += "<div class=\"stat\">Ramki<b id=\"st_pk\">-</b></div></div></div>";

  chunk += "<div class=\"box\"><h3>Wiadomości</h3><div id=\"msgs\"></div><br>";
  chunk += "<form id=\"sendform\" onsubmit=\"sendMsg(event)\">Do (znak): <input type=\"text\" name=\"to\" maxlength=\"9\">";
  chunk += "Treść (maks. 67 znaków): <input type=\"text\" id=\"msgtext\" name=\"text\" maxlength=\"67\">";
  chunk += "<input type=\"submit\" class=\"btn\" value=\"Wyślij wiadomość\"></form><div id=\"sendres\"></div></div>";
  server.sendContent(chunk);

  chunk = "<form action=\"/save\" method=\"POST\">";
  chunk += "<div class=\"box\"><h3>Wygląd i tryb pracy ekranu</h3>";
  chunk += "<div class=\"hint\" style=\"margin-top:0\">Tryb i format zmieniają się od razu po wybraniu z listy. <b id=\"dmode_res\" style=\"color:#28a745\"></b></div>";
  chunk += "<b>Tryb Wyświetlania:</b> <select id=\"dmode\" name=\"dmode\" onchange=\"toggle(); liveDisplay()\">";
  chunk += optionTag(0, appConfig.displayMode, "Klasyczny (Kompas + 2 stacje)");
  chunk += optionTag(1, appConfig.displayMode, "Lista Stacji (Do 6 obiektów)");
  chunk += optionTag(2, appConfig.displayMode, "Statystyki (DX dnia, liczniki, wiadomości)");
  chunk += "</select><br><b>Format Danych:</b> <select id=\"dform\" name=\"dform\" onchange=\"liveDisplay()\">";
  chunk += optionTag(0, appConfig.dataFormat, "Zdekodowane (Współrzędne/Telemetria)");
  chunk += optionTag(1, appConfig.dataFormat, "Surowe (Raw Payload)");
  chunk += optionTag(2, appConfig.dataFormat, "Tylko Komentarz (Wyjątek: WX)");
  chunk += "</select><div id=\"dform_msg\" style=\"display:none;\">* W trybie listy automatycznie wymuszono pokazywanie samego komentarza.</div>";
  chunk += "Pełne odświeżanie e-papieru (min): <input type=\"number\" name=\"refint\" min=\"0\" value=\"" + String(appConfig.refreshInterval) + "\"></div>";
  server.sendContent(chunk);

  chunk = "<div class=\"box\"><h3>Sieć WiFi</h3>" + textInput("SSID:", "wssid", appConfig.wifiSSID);
  chunk += secretInput("Hasło:", "wpass", appConfig.wifiPass.length() > 0);
  chunk += "<h3>Bezpieczeństwo</h3>" + secretInput("Hasło sieci awaryjnej " AP_SSID " (min. 8 znaków):", "appass", appConfig.apPass.length() >= 8);
  chunk += "<input type=\"checkbox\" name=\"apopen\" value=\"1\"> Otwarta sieć awaryjna (bez hasła - niezalecane)<br><br>";
  chunk += secretInput("Hasło panelu WWW (użytkownik: admin):", "webpass", appConfig.webPass.length() > 0);
  chunk += "<input type=\"checkbox\" name=\"webclear\" value=\"1\"> Usuń hasło panelu</div>";
  server.sendContent(chunk);

  chunk = "<div class=\"box\"><h3>Stacja & Filtry</h3>" + textInput("Znak:", "call", appConfig.callsign);
  chunk += textInput("Szerokość (Lat):", "lat", String(appConfig.myLat, 4));
  chunk += textInput("Długość (Lon):", "lon", String(appConfig.myLon, 4));
  chunk += "Maks. Odległość (km, 0=wył): <input type=\"number\" name=\"dist\" min=\"0\" value=\"" + String(appConfig.maxDistKm) + "\"><br><br>";
  chunk += "<input type=\"checkbox\" name=\"hnp\" value=\"1\" " + String(appConfig.hideNonPositional ? "checked" : "") + "> <b>Ukryj ramki bez pozycji</b><br><br>";
  chunk += "<input type=\"checkbox\" name=\"adup\" value=\"1\" " + String(appConfig.antiDuplicate ? "checked" : "") + "> <b>Blokuj duplikaty (10s)</b><br><br>";
  chunk += "<input type=\"checkbox\" name=\"mack\" value=\"1\" " + String(appConfig.msgAck ? "checked" : "") + "> <b>Automatycznie potwierdzaj (ACK) wiadomości do mnie</b><br><br>";
  chunk += textInput("Strefa czasowa (POSIX TZ):", "tz", appConfig.timezone);
  chunk += "<div class=\"hint\">Dla Polski: CET-1CEST,M3.5.0,M10.5.0/3. Czas z NTP służy do zerowania \"DX dnia\" o północy.</div>";
  server.sendContent(chunk);

  chunk = "<b>Złącze Danych:</b> <select id=\"mode\" name=\"mode\" onchange=\"toggle()\">";
  chunk += optionTag(0, appConfig.currentMode, "UART TNC (VP-Digi)");
  chunk += optionTag(1, appConfig.currentMode, "KISS TCP (Klient)");
  chunk += optionTag(2, appConfig.currentMode, "APRS-IS (Internet)");
  chunk += optionTag(3, appConfig.currentMode, "Bluetooth SPP (Klient)");
  chunk += "</select></div>";
  server.sendContent(chunk);

  chunk = "<div class=\"box\" style=\"background-color: #eaf4ff;\"><h3>Nadawanie Beacona</h3>";
  chunk += "<input type=\"checkbox\" name=\"ben\" value=\"1\" " + String(appConfig.beaconEnabled ? "checked" : "") + "> <b>Włącz beacona</b><br><br>";
  chunk += "Odstęp (Minuty): <input type=\"number\" name=\"bint\" min=\"1\" value=\"" + String(appConfig.beaconInterval) + "\">";
  chunk += textInputEx("Symbol (2 znaki np. /-):", "bsym", appConfig.beaconSymbol, "maxlength=\"2\"");
  chunk += textInputEx("Komentarz:", "bcom", appConfig.beaconComment, "maxlength=\"43\"");
  chunk += textInput("Ścieżka RF (beacon i wiadomości):", "bpath", appConfig.beaconPath);
  chunk += "<div class=\"hint\">Np. WIDE1-1,WIDE2-1 dla stacji mobilnej lub WIDE2-1 dla stałej. Puste = bez digipeaterów.</div></div>";
  server.sendContent(chunk);

  chunk = "<div id=\"m_uart\" class=\"box\" style=\"display:none;\"><h3>Tryb: VP-Digi UART</h3>";
  chunk += "Piny połączone sprzętowo (TX:27, RX:26)<br><br><b>Prędkość (Baudrate):</b> <select name=\"baud\">";
  const int bauds[] = {1200, 4800, 9600, 19200, 38400, 115200};
  for (int b : bauds) chunk += optionTag(b, appConfig.uartBaud, (String(b) + " bps").c_str());
  chunk += "</select></div>";
  server.sendContent(chunk);

  chunk = "<div id=\"m_tcp\" class=\"box\" style=\"display:none;\"><h3>Tryb: KISS TCP</h3>" + textInput("IP:", "kip", appConfig.kissIP);
  chunk += "Port: <input type=\"number\" name=\"kport\" value=\"" + String(appConfig.kissPort) + "\"></div>";
  chunk += "<div id=\"m_is\" class=\"box\" style=\"display:none;\"><h3>Tryb: APRS-IS</h3>" + textInput("Serwer:", "isrv", appConfig.isServer);
  chunk += "Port: <input type=\"number\" name=\"isprt\" value=\"" + String(appConfig.isPort) + "\">";
  chunk += textInput("Znak (puste = znak stacji):", "iscall", appConfig.isCall);
  chunk += textInput("Passcode (puste = automatycznie):", "ispass", appConfig.isPass);
  chunk += "<div class=\"hint\">Wyliczony passcode dla " + htmlEscape(effectiveIsCall()) + ": <b>" + String(aprsPasscode(effectiveIsCall())) + "</b></div>";
  chunk += textInput("Filtr serwera (puste = automatyczny):", "isflt", appConfig.isFilter);
  chunk += "<div class=\"hint\">Domyślnie: " + htmlEscape(buildAprsIsFilter()) + " (zasięg z filtra odległości, przy 0 km - 100 km).</div></div>";
  chunk += "<div id=\"m_bt\" class=\"box\" style=\"display:none;\"><h3>Tryb: Bluetooth SPP (Klient)</h3>" + textInput("Adres MAC (Wymagany, np. 98:D3:31...):", "btmac", appConfig.btAddress);
  chunk += textInput("PIN (domyślnie 1234 lub 0000):", "btpin", appConfig.btPin) + "</div>";
  server.sendContent(chunk);

  server.sendContent("<div class=\"hint\">Restart nastąpi tylko przy zmianie połączenia (złącze danych, WiFi, serwer, Bluetooth). Pozostałe ustawienia działają od razu.</div><input type=\"submit\" class=\"btn\" value=\"Zapisz\"></form>");
  server.sendContent("<div class=\"box\" style=\"margin-top:15px;\"><h3>Terminal na żywo</h3><textarea id=\"term\" readonly></textarea></div></body></html>");
  server.sendContent("");
}

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void handleSave() {
  if (!checkAuth()) return;
  prefs.begin("aprs", false);

  int savedDmode = clampInt(server.arg("dmode").toInt(), 0, 2);
  int savedDform = clampInt(server.arg("dform").toInt(), 0, 2);
  if (savedDmode == DISPLAY_LIST) savedDform = 2;

  String call = server.arg("call");
  call.trim(); call.toUpperCase();
  if (call.length() == 0) call = "N0CALL";

  float lat = server.arg("lat").toFloat();
  float lon = server.arg("lon").toFloat();
  if (lat < -90 || lat > 90) lat = appConfig.myLat;
  if (lon < -180 || lon > 180) lon = appConfig.myLon;

  int baud = server.arg("baud").toInt();
  if (baud != 1200 && baud != 4800 && baud != 9600 && baud != 19200 && baud != 38400 && baud != 115200) baud = appConfig.uartBaud;

  String isCall = server.arg("iscall");
  isCall.trim(); isCall.toUpperCase();

  prefs.putInt("dmode", savedDmode);
  prefs.putInt("dform", savedDform);
  prefs.putString("call", call);
  prefs.putFloat("lat", lat);
  prefs.putFloat("lon", lon);
  prefs.putInt("dist", max(0, (int)server.arg("dist").toInt()));
  prefs.putInt("mode", clampInt(server.arg("mode").toInt(), 0, 3));
  prefs.putInt("baud", baud);
  prefs.putString("kip", server.arg("kip"));
  prefs.putInt("kport", clampInt(server.arg("kport").toInt(), 1, 65535));
  prefs.putString("isrv", server.arg("isrv"));
  prefs.putInt("isprt", clampInt(server.arg("isprt").toInt(), 1, 65535));
  prefs.putString("iscall", isCall);
  prefs.putString("ispass", server.arg("ispass"));
  prefs.putString("isflt", server.arg("isflt"));
  prefs.putString("wssid", server.arg("wssid"));
  if (server.arg("wpass").length() > 0) prefs.putString("wpass", server.arg("wpass"));
  prefs.putString("btmac", server.arg("btmac"));
  prefs.putString("btpin", server.arg("btpin"));
  prefs.putBool("ben", server.hasArg("ben"));
  prefs.putInt("bint", max(1, (int)server.arg("bint").toInt()));
  prefs.putString("bsym", server.arg("bsym"));
  prefs.putString("bcom", server.arg("bcom"));
  prefs.putString("bpath", server.arg("bpath"));
  prefs.putBool("hnp", server.hasArg("hnp"));
  prefs.putBool("adup", server.hasArg("adup"));
  prefs.putBool("mack", server.hasArg("mack"));
  prefs.putInt("refint", max(0, (int)server.arg("refint").toInt()));
  if (server.arg("tz").length() > 0) prefs.putString("tz", server.arg("tz"));

  if (server.hasArg("apopen")) prefs.putString("appass", "");
  else if (server.arg("appass").length() >= 8) prefs.putString("appass", server.arg("appass"));

  if (server.hasArg("webclear")) prefs.putString("webpass", "");
  else if (server.arg("webpass").length() > 0) prefs.putString("webpass", server.arg("webpass"));
  prefs.end();

  // Wczytujemy zapisane ustawienia i porównujemy ze starymi: restart tylko wtedy,
  // gdy zmieniło się połączenie (złącze, WiFi, serwer, Bluetooth)
  Config old = appConfig;
  String oldIsCall = effectiveIsCall();
  String oldIsPass = effectiveIsPass();
  String oldFilter = buildAprsIsFilter();
  loadConfig();

  bool needRestart =
      old.currentMode != appConfig.currentMode ||
      old.wifiSSID != appConfig.wifiSSID || old.wifiPass != appConfig.wifiPass ||
      (appConfig.currentMode == MODE_KISS_UART && old.uartBaud != appConfig.uartBaud) ||
      (appConfig.currentMode == MODE_KISS_TCP && (old.kissIP != appConfig.kissIP || old.kissPort != appConfig.kissPort)) ||
      (appConfig.currentMode == MODE_APRS_IS && (old.isServer != appConfig.isServer || old.isPort != appConfig.isPort ||
                                                 oldIsCall != effectiveIsCall() || oldIsPass != effectiveIsPass())) ||
      (appConfig.currentMode == MODE_KISS_BT && (old.btAddress != appConfig.btAddress || old.btPin != appConfig.btPin));

  String note = "";
  if (server.arg("appass").length() > 0 && server.arg("appass").length() < 8) note += "<br>Hasło AP za krótkie (min. 8 znaków) - nie zmieniono.";

  if (needRestart) {
    server.send(200, "text/html", "<html><head><meta charset=\"UTF-8\"><meta http-equiv=\"refresh\" content=\"15;url=/\"></head>"
                "<body style=\"font-family:Arial\">Zapisano. Zmieniło się połączenie - trwa restart..." + note + "</body></html>");
    delay(1000);
    ESP.restart();
  }

  applyLiveSettings(old, oldFilter);
  if (apActive && old.apPass != appConfig.apPass) note += "<br>Nowe hasło sieci awaryjnej zadziała przy jej następnym uruchomieniu.";
  server.send(200, "text/html", "<html><head><meta charset=\"UTF-8\"><meta http-equiv=\"refresh\" content=\"2;url=/\"></head>"
              "<body style=\"font-family:Arial\">Zapisano - zmiany działają od razu, bez restartu." + note + "</body></html>");
}

// Zastosowanie ustawień, które nie wymagają restartu
void applyLiveSettings(const Config &old, const String &oldFilter) {
  if (old.displayMode != appConfig.displayMode || old.dataFormat != appConfig.dataFormat) {
    forceFullRefresh = true;   // inny układ ekranu - pełne odświeżenie bez duchów poprzedniego
  }
  newDataToDisplay = true;

  if (old.timezone != appConfig.timezone && timeConfigured) {
    configTzTime(appConfig.timezone.c_str(), "pool.ntp.org", "time.google.com");
  }

  // Serwer APRS-IS przyjmuje zmianę filtra w trakcie połączenia
  String filter = buildAprsIsFilter();
  if (appConfig.currentMode == MODE_APRS_IS && filter != oldFilter && tcpClient.connected()) {
    tcpClient.print("#filter " + filter + "\r\n");
    addToTerminal("APRS-IS: nowy filtr " + filter);
  }

  // Włączony właśnie beacon - pierwszy za minutę, nie po pełnym interwale
  if (appConfig.beaconEnabled && !old.beaconEnabled) {
    lastBeaconTime = millis() - (unsigned long)appConfig.beaconInterval * 60000UL + 60000UL;
  }

  addToTerminal("> Ustawienia zastosowane bez restartu");
}

// Szybka zmiana trybu ekranu / formatu danych z panelu (bez przycisku Zapisz)
void handleDisplayMode() {
  if (!checkAuth()) return;
  Config old = appConfig;
  if (server.hasArg("dmode")) appConfig.displayMode = clampInt(server.arg("dmode").toInt(), 0, 2);
  if (server.hasArg("dform")) appConfig.dataFormat = clampInt(server.arg("dform").toInt(), 0, 2);
  if (appConfig.displayMode == DISPLAY_LIST) appConfig.dataFormat = 2;

  prefs.begin("aprs", false);
  prefs.putInt("dmode", appConfig.displayMode);
  prefs.putInt("dform", appConfig.dataFormat);
  prefs.end();

  if (old.displayMode != appConfig.displayMode || old.dataFormat != appConfig.dataFormat) {
    forceFullRefresh = true;
    newDataToDisplay = true;
    lastDisplayUpdate = 0;   // bez czekania na 3-sekundowy odstęp między odświeżeniami
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleStatus() {
  if (!checkAuth()) return;
  // Panel otwarty na ekranie pokazuje wiadomości - kasujemy licznik MSG na e-papierze
  if (server.arg("seen") == "1" && unreadMessages > 0) {
    unreadMessages = 0;
    newDataToDisplay = true;
  }
  unsigned long now = millis();
  String j = "{\"dx\":{\"call\":" + jsonEscape(dxCall) + ",\"km\":" + String(dxDistance < 0 ? 0 : dxDistance, 1) + "}";
  j += ",\"heard1h\":" + String(countHeardWithin(3600000UL));
  j += ",\"heard24h\":" + String(countHeardWithin(86400000UL));
  j += ",\"packets\":" + String(totalPackets);
  j += ",\"uptime\":" + String(now / 1000);
  j += ",\"messages\":[";
  bool first = true;
  for (int i = 0; i < MAX_MESSAGES; i++) {
    const MessageEntry &m = messages[i];
    if (m.text.length() == 0) continue;
    if (!first) j += ",";
    first = false;
    j += "{\"from\":" + jsonEscape(m.from) + ",\"to\":" + jsonEscape(m.to) + ",\"text\":" + jsonEscape(m.text);
    j += ",\"out\":" + String(m.outgoing ? "true" : "false") + ",\"acked\":" + String(m.acked ? "true" : "false");
    j += ",\"age\":" + jsonEscape(formatAge(now - m.ms)) + "}";
  }
  j += "]}";
  server.send(200, "application/json", j);
}

void handleStations() {
  if (!checkAuth()) return;
  unsigned long now = millis();
  String j = "{\"me\":{\"call\":" + jsonEscape(appConfig.callsign) + ",\"lat\":" + String(appConfig.myLat, 5) + ",\"lon\":" + String(appConfig.myLon, 5) + "}";
  j += ",\"range\":" + String(appConfig.maxDistKm) + ",\"stations\":[";
  bool first = true;
  for (int i = 0; i < MAX_HEARD; i++) {
    const HeardStation &h = heard[i];
    if (h.call.length() == 0 || h.distance < 0) continue;
    if (!first) j += ",";
    first = false;
    j += "{\"call\":" + jsonEscape(h.call) + ",\"lat\":" + String(h.lat, 5) + ",\"lon\":" + String(h.lon, 5);
    j += ",\"km\":" + String(h.distance, 1) + ",\"brg\":" + String(h.bearing, 0);
    j += ",\"radio\":" + jsonEscape(h.radio) + ",\"text\":" + jsonEscape(h.text);
    j += ",\"age\":" + String((now - h.lastHeard) / 1000) + ",\"packets\":" + String(h.packets) + "}";
  }
  j += "]}";
  server.send(200, "application/json", j);
}

void handleSend() {
  if (!checkAuth()) return;
  String err = queueOutgoingMessage(server.arg("to"), server.arg("text"));
  if (err.length() == 0) server.send(200, "application/json", "{\"ok\":true}");
  else server.send(200, "application/json", "{\"ok\":false,\"error\":" + jsonEscape(err) + "}");
}

void setupWebEndpoints() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/send", HTTP_POST, handleSend);
  server.on("/display", HTTP_POST, handleDisplayMode);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/stations", HTTP_GET, handleStations);
  server.on("/map", HTTP_GET, []() {
    if (!checkAuth()) return;
    server.send_P(200, "text/html", MAP_PAGE);
  });
  server.on("/terminal", HTTP_GET, []() {
    if (!checkAuth()) return;
    server.send(200, "text/plain", getTerminalText());
  });
}
