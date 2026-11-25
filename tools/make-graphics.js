// Generator grafik do README (docs/images): wizualizacje ekranów wierne układowi z display.ino,
// baner i schemat połączeń. Użycie: node tools/make-graphics.js .
const fs = require('fs');
const path = require('path');

const root = process.argv[2];
const out = path.join(root, 'docs', 'images');
fs.mkdirSync(out, { recursive: true });

// --- Ikony APRS z icons_aprs.h ---
const iconsSrc = fs.readFileSync(path.join(root, 'icons_aprs.h'), 'utf8');
function table(name) {
  const start = iconsSrc.indexOf(name);
  const body = iconsSrc.slice(start, iconsSrc.indexOf('};', start));
  return [...body.matchAll(/\{([^}]*)\}/g)].map(m => m[1].split(',').map(x => parseInt(x, 16)));
}
const PRIMARY = table('aprs_symbols_primary');
const ALTERNATE = table('aprs_symbols_alternate');

const INK = '#1b1b1b';
const PAPER = '#ecebe4';
const FONT = 'Helvetica, Arial, sans-serif';
// Rozmiary czcionek U8g2 (wysokość wielkiej litery) -> CSS font-size
const F = { big: 35, title: 26.5, body: 18.5, label: 16 };

function esc(s) { return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); }

function iconSvg(x, y, tableChar, code, onBlack) {
  const bits = (tableChar === '/' ? PRIMARY : ALTERNATE)[code.charCodeAt(0) - 33];
  let s = '';
  if (onBlack) s += `<rect x="${x - 3}" y="${y - 3}" width="38" height="38" rx="5" fill="${PAPER}"/>`;
  let d = '';
  for (let r = 0; r < 32; r++) for (let c = 0; c < 32; c++) {
    if (bits[r * 4 + (c >> 3)] & (0x80 >> (c & 7))) d += `M${x + c} ${y + r}h1v1h-1z`;
  }
  s += `<path d="${d}" fill="${INK}" shape-rendering="crispEdges"/>`;
  const overlay = /[0-9A-Z]/.test(tableChar);
  if (overlay) {
    s += `<rect x="${x + 9}" y="${y + 8}" width="14" height="16" fill="${PAPER}" stroke="${INK}"/>`;
    s += text(x + 16, y + 21, tableChar, F.label, { anchor: 'middle' });
  }
  return s;
}

function text(x, y, str, size, o = {}) {
  const fill = o.fill || INK;
  const anchor = o.anchor ? ` text-anchor="${o.anchor}"` : '';
  const weight = o.bold ? ' font-weight="bold"' : '';
  return `<text x="${x}" y="${y}" font-family="${FONT}" font-size="${size}" fill="${fill}"${anchor}${weight}>${esc(str)}</text>`;
}
const bar = (y, h) => `<rect x="0" y="${y}" width="400" height="${h}" fill="${INK}"/>`;
const hline = (x1, y, x2) => `<line x1="${x1}" y1="${y + 0.5}" x2="${x2}" y2="${y + 0.5}" stroke="${INK}" stroke-width="1" shape-rendering="crispEdges"/>`;

function footer(badge) {
  let s = bar(275, 25);
  let x = 5;
  if (badge) {
    s += `<rect x="3" y="278" width="62" height="19" rx="4" fill="${PAPER}"/>` + text(9, 293, badge, F.label);
    x = 72;
  }
  return s + text(x, 293, 'SP3WRO | KISS TCP | 192.168.1.50', F.label, { fill: PAPER });
}

// --- Ekran klasyczny ---
function screenClassic() {
  let s = bar(0, 50) + text(10, 37, 'SP3WRO-9', F.big, { fill: PAPER }) + iconSvg(352, 9, '/', '>', true);
  const cx = 75, cy = 125, r = 40;
  s += `<circle cx="${cx}" cy="${cy}" r="${r - 0.5}" fill="none" stroke="${INK}" stroke-width="2"/>`;
  s += `<line x1="${cx}" y1="${cy - r}" x2="${cx}" y2="${cy + r}" stroke="${INK}"/><line x1="${cx - r}" y1="${cy}" x2="${cx + r}" y2="${cy}" stroke="${INK}"/>`;
  s += text(cx, cy - r - 4, 'N', F.label, { anchor: 'middle' }) + text(cx, cy + r + 14, 'S', F.label, { anchor: 'middle' });
  s += text(cx + r + 5, cy + 4, 'E', F.label) + text(cx - r - 5, cy + 4, 'W', F.label, { anchor: 'end' });
  const brg = 118 * Math.PI / 180;
  s += `<circle cx="${(cx + Math.sin(brg) * (r - 8)).toFixed(1)}" cy="${(cy - Math.cos(brg) * (r - 8)).toFixed(1)}" r="6" fill="${INK}"/>`;
  s += text(cx, cy + r + 35, '23.4 km', F.title, { anchor: 'middle' });
  ['Mic-E (52.3052, 17.5365)', '[En Route | 54km/h, 118st]', 'Alt: 92m QRV 145.500', 'Jadę do Poznania'].forEach((l, i) => {
    s += text(160, 85 + i * 20, l, F.body);
  });
  s += hline(0, 205, 400) + `<line x1="250.5" y1="205" x2="250.5" y2="250" stroke="${INK}"/>`;
  s += text(5, 225, 'SQ3ABC-9 (52km)', F.label) + text(5, 247, 'SR3XYZ (18km)', F.label);
  s += text(256, 228, 'FT5D', F.title) + text(256, 249, 'DX: OK1ABC-9', F.label) + text(256, 269, '182km SW', F.label);
  return s + footer();
}

// --- Lista stacji ---
function screenList() {
  let s = bar(0, 30) + text(6, 23, 'STACJE', F.title, { fill: PAPER });
  s += text(394, 21, 'DX OK1ABC-9 182km SW', F.label, { fill: PAPER, anchor: 'end' });
  const rows = [
    ['/', '>', 'SP3WRO-9 [23km] [SE] [FT5D]', 'teraz', 'QRV 145.500, jadę do Poznania'],
    ['/', '_', 'SP3WX-13 [8km] [N]', '2min', 'Temp: 3 C | Wilgotność: 83 %'],
    ['L', '&', 'SR3LRG [15km] [W] [LoRa iG..', '4min', 'LoRa APRS iGate 433.775'],
    ['/', 'v', 'SQ3ABC-9 [52km] [NE] [APRS..', '7min', 'Wycieczka rodzinna'],
    ['/', '#', 'SR3XYZ [18km] [S] [MicroSat]', '12min', 'Digi WIDE1-1 / WIDE2-1'],
    ['/', '-', 'SP3DEF [4km] [E]', '25min', 'Stacja domowa, QTH Poznań'],
  ];
  rows.forEach(([t, c, head, age, body], i) => {
    const top = 33 + i * 40;
    s += iconSvg(4, top + 4, t, c, false);
    s += text(42, top + 16, head, F.body) + text(395, top + 16, age, F.body, { anchor: 'end' });
    s += text(42, top + 34, body, F.label);
    s += hline(5, top + 39, 395);
  });
  return s + footer();
}

// --- Statystyki ---
function screenStats() {
  let s = bar(0, 30) + text(6, 23, 'STATYSTYKI APRS', F.title, { fill: PAPER }) + text(394, 23, '18:42', F.title, { fill: PAPER, anchor: 'end' });
  s += text(5, 52, 'DX dnia (o 14:05):', F.label);
  s += text(5, 88, 'OK1ABC-9', F.big) + text(395, 88, '182km SW', F.title, { anchor: 'end' });
  s += hline(0, 100, 400);
  s += text(5, 122, 'Stacje 1h: 14   24h: 57', F.label) + text(5, 142, 'Ramki: 1342   Uptime: 9h', F.label);
  s += hline(0, 152, 400);
  s += text(5, 172, 'Od SQ3ABC (5min temu)', F.label);
  s += text(5, 194, 'Cześć! Słychać Cię dobrze przez SR3XYZ,', F.label) + text(5, 214, 'dzięki za ACK. 73!', F.label);
  return s + footer('MSG 1');
}

const screenRect = `<rect width="400" height="300" fill="${PAPER}"/>`;

// Ekran w czerwonej ramce wzorowanej na obudowie urządzenia
function framed(content, scale = 1) {
  const pad = 34, top = 62, bottom = 50;
  const W = 400 + 2 * pad, H = 300 + top + bottom;
  const screw = (x, y) => `<circle cx="${x}" cy="${y}" r="7" fill="#c9cdd2" stroke="#8a8f96" stroke-width="1.5"/><path d="M${x - 4} ${y}h8M${x} ${y - 4}v8" stroke="#6b7078" stroke-width="1.6"/>`;
  return {
    W: W * scale, H: H * scale,
    body: `<g transform="scale(${scale})">
  <rect x="0" y="0" width="${W}" height="${H}" rx="18" fill="#d8262c"/>
  <rect x="3" y="3" width="${W - 6}" height="${H - 6}" rx="16" fill="none" stroke="#b81d22" stroke-width="2"/>
  <text x="${W / 2}" y="40" font-family="${FONT}" font-size="25" font-weight="bold" fill="#b81d22" text-anchor="middle">APRS Display System</text>
  <text x="${W / 2}" y="38.5" font-family="${FONT}" font-size="25" font-weight="bold" fill="#e8484d" text-anchor="middle" opacity="0.6">APRS Display System</text>
  ${screw(22, 22)}${screw(W - 22, 22)}${screw(22, H - 22)}${screw(W - 22, H - 22)}
  <rect x="${pad - 10}" y="${top - 10}" width="420" height="320" rx="4" fill="#9a9a96"/>
  <g transform="translate(${pad},${top})">${screenRect}${content}</g>
</g>`,
  };
}

function writeSvg(name, w, h, body, title) {
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="0 0 ${w} ${h}" role="img" aria-label="${esc(title)}"><title>${esc(title)}</title>${body}</svg>\n`;
  fs.writeFileSync(path.join(out, name), svg);
  console.log(name, (svg.length / 1024).toFixed(1) + ' KB');
}

for (const [name, fn, title] of [
  ['screen-classic.svg', screenClassic, 'Tryb klasyczny / Classic mode'],
  ['screen-list.svg', screenList, 'Lista stacji / Station list'],
  ['screen-stats.svg', screenStats, 'Statystyki / Statistics'],
]) {
  const f = framed(fn(), 1);
  writeSvg(name, f.W, f.H, f.body, title);
}

// --- Baner ---
{
  const W = 1280, H = 400;
  const dev = framed(screenClassic(), 0.72);
  const body = `<defs>
  <linearGradient id="bg" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#14181f"/><stop offset="1" stop-color="#232b38"/></linearGradient>
  <pattern id="grid" width="32" height="32" patternUnits="userSpaceOnUse"><path d="M32 0H0V32" fill="none" stroke="#ffffff" stroke-opacity="0.04"/></pattern>
</defs>
<rect width="${W}" height="${H}" rx="22" fill="url(#bg)"/>
<rect width="${W}" height="${H}" rx="22" fill="url(#grid)"/>
<g fill="none" stroke="#d8262c" stroke-opacity="0.35" stroke-width="2">
  <circle cx="560" cy="330" r="40"/><circle cx="560" cy="330" r="75" stroke-opacity="0.22"/><circle cx="560" cy="330" r="110" stroke-opacity="0.12"/>
</g>
<text x="70" y="150" font-family="${FONT}" font-size="64" font-weight="bold" fill="#ffffff">APRS Display System</text>
<text x="72" y="200" font-family="${FONT}" font-size="26" fill="#c6ccd6">Wyświetlacz ramek APRS na ESP32 i e-papierze 4,2"</text>
<text x="72" y="236" font-family="${FONT}" font-size="22" fill="#8a94a6">APRS frame display · ESP32 · 4.2" e-ink</text>
<g font-family="${FONT}" font-size="17" font-weight="bold">
  ${['KISS UART', 'KISS TCP', 'APRS-IS', 'Bluetooth'].map((t, i) => {
    const x = 72 + i * 128;
    return `<rect x="${x}" y="276" width="116" height="34" rx="17" fill="#d8262c" fill-opacity="${i === 0 ? 1 : 0.85}"/><text x="${x + 58}" y="298" fill="#fff" text-anchor="middle">${t}</text>`;
  }).join('')}
</g>
<text x="72" y="350" font-family="${FONT}" font-size="18" fill="#8a94a6">v2.0 · SP3WRO</text>
<g transform="translate(${W - dev.W - 60},${(H - dev.H) / 2}) rotate(-3 ${dev.W / 2} ${dev.H / 2})">
  <rect x="8" y="12" width="${dev.W}" height="${dev.H}" rx="14" fill="#000" opacity="0.35"/>
  ${dev.body}
</g>`;
  writeSvg('banner.svg', W, H, body, 'APRS Display System');
}

// --- Schemat połączeń ---
{
  const W = 1000, H = 520;
  const box = (x, y, w, h, title, sub, color) =>
    `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="12" fill="${color}" stroke="#1f2937" stroke-width="2"/>` +
    `<text x="${x + w / 2}" y="${y + 30}" font-family="${FONT}" font-size="20" font-weight="bold" fill="#fff" text-anchor="middle">${title}</text>` +
    `<text x="${x + w / 2}" y="${y + 52}" font-family="${FONT}" font-size="14" fill="#e5e7eb" text-anchor="middle">${sub}</text>`;
  const pin = (x, y, label, anchor) =>
    `<circle cx="${x}" cy="${y}" r="5" fill="#f3f4f6" stroke="#1f2937" stroke-width="1.5"/>` +
    `<text x="${anchor === 'end' ? x - 12 : x + 12}" y="${y + 5}" font-family="${FONT}" font-size="15" fill="#fff" text-anchor="${anchor}">${label}</text>`;

  const epd = [['VCC', '3V3', '#dc2626'], ['GND', 'GND', '#111827'], ['SDA (DIN)', 'GPIO 23', '#2563eb'], ['SCL (CLK)', 'GPIO 18', '#f59e0b'],
               ['CS', 'GPIO 5', '#16a34a'], ['DC', 'GPIO 17', '#9333ea'], ['RST', 'GPIO 16', '#0891b2'], ['BUSY', 'GPIO 4', '#db2777']];
  const tnc = [['TX', 'GPIO 26 (RX)', '#2563eb'], ['RX', 'GPIO 27 (TX)', '#f59e0b'], ['GND', 'GND', '#111827']];

  let s = `<rect width="${W}" height="${H}" rx="18" fill="#f8fafc"/>`;
  s += box(40, 60, 220, 400, 'E-ink 4,2"', 'WeAct / GDEY042T81 (SPI)', '#374151');
  s += box(390, 40, 220, 440, 'ESP32', 'WROOM-32 DevKit', '#1e3a8a');
  s += box(740, 140, 220, 220, 'Modem / TNC', 'VP-Digi (KISS, UART2)', '#7f1d1d');

  epd.forEach(([a, b, color], i) => {
    const y = 140 + i * 40;
    const y2 = 120 + i * 40;
    s += `<path d="M260 ${y} C 325 ${y}, 325 ${y2}, 390 ${y2}" fill="none" stroke="${color}" stroke-width="4" stroke-linecap="round"/>`;
    s += pin(260, y, a, 'end') + pin(390, y2, b, 'start');
  });
  tnc.forEach(([a, b, color], i) => {
    const y = 210 + i * 45;
    const y2 = 445 - (2 - i) * 40;
    s += `<path d="M740 ${y} C 675 ${y}, 675 ${y2}, 610 ${y2}" fill="none" stroke="${color}" stroke-width="4" stroke-linecap="round"/>`;
    s += pin(740, y, a, 'start') + pin(610, y2, b, 'end');
  });
  s += `<text x="${W / 2}" y="505" font-family="${FONT}" font-size="15" fill="#475569" text-anchor="middle">Ekran zasilany z 3,3 V (nigdy 5 V) · TX modemu do RX ESP32 i odwrotnie · wspólna masa obowiązkowa</text>`;
  writeSvg('wiring.svg', W, H, s, 'Schemat połączeń / Wiring diagram');
}
