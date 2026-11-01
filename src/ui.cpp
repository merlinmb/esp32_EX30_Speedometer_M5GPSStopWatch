// Rendering for the round 466x466 AMOLED. Everything is drawn into a PSRAM sprite
// and pushed in one go to avoid flicker. Colours come from settings.day / settings.night.
#include "ui.h"
#include <M5Unified.h>
#include "fonts_vlw.h"

static constexpr int W = 466, CX = 233, CY = 233;
static M5Canvas canvas(&M5.Display);

static lgfx::PointerWrapper wSpeed, wSpeedM, wLabel, wSmall, wTitle;
static lgfx::VLWfont fSpeed, fSpeedM, fLabel, fSmall, fTitle;

static inline uint16_t c565(uint32_t rgb) {
  return lgfx::color565((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

// Design angle: 0 = 12 o'clock, clockwise. LovyanGFX: 0 = 3 o'clock, clockwise.
static inline float lg(float designDeg) { return designDeg + 270.0f; }
static inline void polar(float r, float designDeg, int& x, int& y) {
  float a = designDeg * DEG_TO_RAD;
  x = (int)lroundf(CX + r * sinf(a));
  y = (int)lroundf(CY - r * cosf(a));
}
static constexpr float ARC_START = -135, ARC_SWEEP = 270;

void uiBegin() {
  canvas.setColorDepth(16);
  canvas.setPsram(true);
  canvas.createSprite(W, W);
  wSpeed.set(font_speed, sizeof(font_speed));
  wSpeedM.set(font_speed_m, sizeof(font_speed_m));
  wLabel.set(font_label, sizeof(font_label));
  wSmall.set(font_small, sizeof(font_small));
  wTitle.set(font_title, sizeof(font_title));
  fSpeed.loadFont(&wSpeed);
  fSpeedM.loadFont(&wSpeedM);
  fLabel.loadFont(&wLabel);
  fSmall.loadFont(&wSmall);
  fTitle.loadFont(&wTitle);
}

// Text with extra letter spacing (for the units label), centred on x.
static void drawSpaced(const char* s, int cx, int cy, int spacing) {
  int total = 0;
  char ch[2] = {0, 0};
  for (const char* p = s; *p; ++p) { ch[0] = *p; total += canvas.textWidth(ch) + spacing; }
  total -= spacing;
  int x = cx - total / 2;
  canvas.setTextDatum(textdatum_t::middle_left);
  for (const char* p = s; *p; ++p) {
    ch[0] = *p;
    canvas.drawString(ch, x, cy);
    x += canvas.textWidth(ch) + spacing;
  }
}

static void satIcon(int x, int y, uint16_t col, uint16_t bg) {
  // satellite dot with two signal arcs above it
  canvas.fillSmoothCircle(x, y + 4, 3, col);
  canvas.fillArc(x, y + 4, 9, 7, 220, 320, col);
  canvas.fillArc(x, y + 4, 15, 13, 225, 315, col);
}

static void moonIcon(int x, int y, uint16_t col, uint16_t bg) {
  canvas.fillSmoothCircle(x, y, 7, col);
  canvas.fillSmoothCircle(x + 4, y - 3, 6, bg);
}

// Status line: [icon] left | right, centred.
static void statusLine(int cy, bool moon, const char* left, const char* right,
                       uint16_t txt, uint16_t icon, uint16_t sep, uint16_t bg) {
  canvas.setFont(&fSmall);
  int wl = canvas.textWidth(left);
  int wr = right && right[0] ? canvas.textWidth(right) : 0;
  int wsep = wr ? canvas.textWidth("|") + 20 : 0;
  int total = 28 + wl + wsep + wr;
  int x = CX - total / 2;
  if (moon) moonIcon(x + 7, cy, icon, bg);
  else satIcon(x + 10, cy - 1, icon, bg);
  x += 28;
  canvas.setTextDatum(textdatum_t::middle_left);
  canvas.setTextColor(txt);
  canvas.drawString(left, x, cy);
  x += wl;
  if (wr) {
    canvas.setTextColor(sep);
    canvas.drawString("|", x + 10, cy);
    x += wsep;
    canvas.setTextColor(txt);
    canvas.drawString(right, x, cy);
  }
}

static uint16_t dim(uint32_t rgb, float k) {
  return lgfx::color565(((rgb >> 16) & 0xFF) * k, ((rgb >> 8) & 0xFF) * k, (rgb & 0xFF) * k);
}

// ---------------- Design A: Clean (day) ----------------
static void drawDay(const UiModel& m) {
  const ThemeColours& t = settings.day;
  uint16_t bg = c565(t.background);
  canvas.fillScreen(bg);

  float frac = constrain(m.ringValue / m.ringMax, 0.0f, 1.0f);
  uint16_t fill = m.overLimit ? c565(t.alert) : c565(t.ringFill);
  uint16_t track = c565(t.ringTrack);
  const int R = 218, HW = 4;
  canvas.fillArc(CX, CY, R + HW, R - HW, lg(ARC_START), lg(ARC_START + ARC_SWEEP), track);
  int x, y;
  polar(R, ARC_START, x, y);              canvas.fillSmoothCircle(x, y, HW, track);
  polar(R, ARC_START + ARC_SWEEP, x, y);  canvas.fillSmoothCircle(x, y, HW, track);
  if (m.fix && frac > 0.002f) {
    float end = ARC_START + ARC_SWEEP * frac;
    canvas.fillArc(CX, CY, R + HW, R - HW, lg(ARC_START), lg(end), fill);
    polar(R, ARC_START, x, y); canvas.fillSmoothCircle(x, y, HW, fill);
    polar(R, end, x, y);       canvas.fillSmoothCircle(x, y, HW, fill);
  }

  // Status
  char left[24], right[16];
  if (!m.gpsConnected) { snprintf(left, sizeof(left), "NO GPS"); right[0] = 0; }
  else if (!m.fix) { snprintf(left, sizeof(left), "SEARCHING %u", m.sats); right[0] = 0; }
  else { snprintf(left, sizeof(left), "%u SATS", m.sats); snprintf(right, sizeof(right), "%.0f Hz", m.hz); }
  statusLine(70, false, left, right, c565(t.status), m.fix ? c565(t.statusIcon) : dim(t.status, 0.6f), dim(t.status, 0.5f), bg);

  // Speed
  canvas.setFont(&fSpeed);
  canvas.setTextDatum(textdatum_t::baseline_center);
  char sp[8];
  if (m.fix) snprintf(sp, sizeof(sp), "%d", m.speed); else strcpy(sp, "--");
  canvas.setTextColor(m.fix ? (m.overLimit ? c565(t.alert) : c565(t.speed)) : c565(t.ringTrack) | 0x4208);
  canvas.drawString(sp, CX, 300);

  // Units
  canvas.setFont(&fLabel);
  canvas.setTextColor(c565(t.unit));
  drawSpaced(m.units, CX, 342, 7);

  // Bottom line
  canvas.setFont(&fSmall);
  canvas.setTextDatum(textdatum_t::middle_center);
  char line[48];
  if (m.toast[0]) { canvas.setTextColor(c565(t.ringFill)); strncpy(line, m.toast, sizeof(line)); }
  else { canvas.setTextColor(c565(t.info)); snprintf(line, sizeof(line), "%s \xC2\xB7 %s", m.clock, m.trip); }
  line[sizeof(line) - 1] = 0;
  canvas.drawString(line, CX, 392);
}


// ---------------- Design B: Gauge (option 1 outline, option 2 solid) ----------------
static void drawGauge(const UiModel& m, bool solid) {
  const ThemeColours& t = settings.day;
  uint16_t bg = c565(t.background);
  canvas.fillScreen(bg);

  const bool kmh = settings.units == Units::KMH;
  const float vmax = m.ringMax;
  const int majorEvery = kmh ? 40 : 20, midEvery = kmh ? 20 : 10, minorEvery = kmh ? 10 : 5;
  auto angOf = [&](float v) { return ARC_START + ARC_SWEEP * constrain(v / vmax, 0.0f, 1.0f); };

  const int R = solid ? 202 : 206, HW = solid ? 11 : 7;
  uint16_t fill = m.overLimit ? c565(t.alert) : c565(t.ringFill);
  canvas.fillArc(CX, CY, R + HW, R - HW, lg(ARC_START), lg(ARC_START + ARC_SWEEP), c565(t.ringTrack));
  if (m.fix && m.ringValue > 0.3f) {
    canvas.fillArc(CX, CY, R + HW, R - HW, lg(ARC_START), lg(angOf(m.ringValue)), fill);
    if (m.overLimit && m.limit > 0) {
      // brighter section beyond the limit
      uint32_t a = t.alert;
      uint16_t hi = lgfx::color565(min(255, (int)((a >> 16) & 0xFF) + 60), min(255, (int)((a >> 8) & 0xFF) + 60), min(255, (int)(a & 0xFF) + 60));
      canvas.fillArc(CX, CY, R + HW, R - HW, lg(angOf(m.limit)), lg(angOf(m.ringValue)), hi);
    }
  }

  // Ticks and numbers
  canvas.setFont(&fSmall);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(c565(t.tickLabel));
  const int tickOuter = R - HW - 6;
  for (int v = 0; v <= (int)vmax; v += minorEvery) {
    bool major = v % majorEvery == 0, mid = v % midEvery == 0;
    if (solid && !major) continue;
    float a = angOf(v);
    int x0, y0, x1, y1;
    if (solid) {
      polar(tickOuter - 4, a, x0, y0);
      canvas.fillSmoothCircle(x0, y0, 3, c565(t.tickMajor));
    } else {
      int len = major ? 16 : (mid ? 10 : 6);
      polar(tickOuter - len, a, x0, y0);
      polar(tickOuter, a, x1, y1);
      uint16_t col = major ? c565(t.tickMajor) : c565(t.tickMinor);
      if (major) canvas.drawWideLine(x0, y0, x1, y1, 1.5f, col);
      else canvas.drawSmoothLine(x0, y0, x1, y1, col);
    }
    if (major) {
      polar(solid ? tickOuter - 28 : tickOuter - 36, a, x0, y0);
      char b[6];
      snprintf(b, sizeof(b), "%d", v);
      canvas.drawString(b, x0, y0);
    }
  }

  // Limit marker outside the ring
  if (m.limit > 0 && m.limit <= vmax) {
    float a = angOf(m.limit);
    int px, py, ax, ay, bx, by;
    polar(R + HW + 2, a, px, py);
    polar(R + HW + 14, a - 3.2f, ax, ay);
    polar(R + HW + 14, a + 3.2f, bx, by);
    canvas.fillTriangle(px, py, ax, ay, bx, by, c565(t.limitMark));
    polar(R - HW, a, ax, ay);
    polar(R + HW, a, bx, by);
    canvas.drawWideLine(ax, ay, bx, by, 1.5f, c565(t.limitMark));
  }

  // Speed
  canvas.setFont(&fSpeedM);
  canvas.setTextDatum(textdatum_t::baseline_center);
  char sp[8];
  if (m.fix) snprintf(sp, sizeof(sp), "%d", m.speed); else strcpy(sp, "--");
  canvas.setTextColor(!m.fix ? c565(t.tickMinor) : (m.overLimit ? c565(t.alert) : c565(t.speed)));
  canvas.drawString(sp, CX, 272);
  canvas.setFont(&fLabel);
  canvas.setTextColor(c565(t.unit));
  drawSpaced(m.units, CX, 310, 6);

  // Pill: limit / over limit / clock and trip / toast
  canvas.setFont(&fSmall);
  canvas.setTextDatum(textdatum_t::middle_center);
  char pill[40];
  const int py = 369, ph = 34;
  if (m.toast[0]) {
    canvas.setTextColor(c565(t.ringFill));
    canvas.drawString(m.toast, CX, py);
  } else if (m.overLimit) {
    snprintf(pill, sizeof(pill), "+%d OVER %d", m.speed - m.limit, m.limit);
    int w = canvas.textWidth(pill) + 36;
    canvas.fillSmoothRoundRect(CX - w / 2, py - ph / 2, w, ph, ph / 2, c565(t.alert));
    canvas.setTextColor(bg);
    canvas.drawString(pill, CX, py);
  } else if (m.limit > 0) {
    snprintf(pill, sizeof(pill), "LIMIT %d", m.limit);
    int w = canvas.textWidth(pill) + 36;
    if (solid) {
      canvas.fillSmoothRoundRect(CX - w / 2, py - ph / 2, w, ph, ph / 2, c565(t.limitMark));
      canvas.setTextColor(bg);
    } else {
      canvas.fillSmoothRoundRect(CX - w / 2, py - ph / 2, w, ph, ph / 2, c565(t.limitMark));
      canvas.fillSmoothRoundRect(CX - w / 2 + 2, py - ph / 2 + 2, w - 4, ph - 4, ph / 2 - 2, bg);
      canvas.setTextColor(c565(t.limitMark));
    }
    canvas.drawString(pill, CX, py);
  } else {
    canvas.setTextColor(c565(t.info));
    snprintf(pill, sizeof(pill), "%s \xC2\xB7 %s", m.clock, m.trip);
    canvas.drawString(pill, CX, py);
  }

  // Satellites, small, at the bottom gap of the dial
  char st[20];
  if (!m.gpsConnected) strcpy(st, "NO GPS");
  else snprintf(st, sizeof(st), m.fix ? "%u SATS" : "SEARCHING %u", m.sats);
  canvas.setTextColor(m.fix ? c565(t.status) : c565(t.tickMinor));
  canvas.drawString(st, CX, 418);
}

// ---------------- Design C: Info ----------------
static void tile(int cx, int y, const char* label, const char* value, const char* unit, const ThemeColours& t) {
  canvas.setFont(&fSmall);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(c565(t.status));
  canvas.drawString(label, cx, y);
  canvas.setFont(&fTitle);
  int wv = canvas.textWidth(value);
  canvas.setFont(&fSmall);
  int wu = unit[0] ? canvas.textWidth(unit) + 3 : 0;
  int x = cx - (wv + wu) / 2;
  canvas.setFont(&fTitle);
  canvas.setTextDatum(textdatum_t::baseline_left);
  canvas.setTextColor(c565(t.speed));
  canvas.drawString(value, x, y + 36);
  if (wu) {
    canvas.setFont(&fSmall);
    canvas.setTextColor(c565(t.unit));
    canvas.drawString(unit, x + wv + 3, y + 36);
  }
}

static void drawInfo(const UiModel& m) {
  const ThemeColours& t = settings.day;
  uint16_t bg = c565(t.background);
  canvas.fillScreen(bg);

  const int R = 222, HW = 2;
  canvas.fillArc(CX, CY, R + HW, R - HW, lg(ARC_START), lg(ARC_START + ARC_SWEEP), c565(t.ringTrack));
  float frac = constrain(m.ringValue / m.ringMax, 0.0f, 1.0f);
  if (m.fix && frac > 0.002f)
    canvas.fillArc(CX, CY, R + HW, R - HW, lg(ARC_START), lg(ARC_START + ARC_SWEEP * frac), m.overLimit ? c565(t.alert) : c565(t.ringFill));

  // Top line: clock, sats, HDOP
  canvas.setFont(&fSmall);
  canvas.setTextDatum(textdatum_t::middle_center);
  char top[48];
  if (!m.gpsConnected) snprintf(top, sizeof(top), "%s   NO GPS", m.clock);
  else if (!m.fix) snprintf(top, sizeof(top), "%s   SEARCHING %u", m.clock, m.sats);
  else snprintf(top, sizeof(top), "%s   %u sats   HDOP %.1f", m.clock, m.sats, m.hdop);
  canvas.setTextColor(c565(t.status));
  canvas.drawString(top, CX, 56);

  canvas.setFont(&fSpeedM);
  canvas.setTextDatum(textdatum_t::baseline_center);
  char sp[8];
  if (m.fix) snprintf(sp, sizeof(sp), "%d", m.speed); else strcpy(sp, "--");
  canvas.setTextColor(!m.fix ? c565(t.ringTrack) | 0x4208 : (m.overLimit ? c565(t.alert) : c565(t.speed)));
  canvas.drawString(sp, CX, 206);

  canvas.setFont(&fSmall);
  canvas.setTextDatum(textdatum_t::middle_center);
  char hl[40];
  if (m.toast[0]) { canvas.setTextColor(c565(t.ringFill)); strncpy(hl, m.toast, sizeof(hl) - 1); hl[sizeof(hl) - 1] = 0; }
  else { canvas.setTextColor(c565(t.unit)); snprintf(hl, sizeof(hl), "%s \xC2\xB7 %s", m.units, m.fix ? m.heading : "--"); }
  canvas.drawString(hl, CX, 244);

  // Dividers and tiles
  canvas.drawFastHLine(83, 276, 300, c565(t.ringTrack));
  canvas.drawFastVLine(CX, 288, 120, c565(t.ringTrack));
  const char* u = settings.units == Units::MPH ? "mph" : "km/h";
  tile(168, 298, "TRIP", m.tripVal, m.tripUnit, t);
  tile(298, 298, "MAX", m.maxVal, u, t);
  tile(168, 360, "AVG", m.avgVal, u, t);
  tile(298, 360, "ALT", m.altVal, "m", t);
}

// ---------------- Design D: Night ----------------
static void drawNight(const UiModel& m) {
  const ThemeColours& t = settings.night;
  uint16_t bg = c565(t.background);
  canvas.fillScreen(bg);

  const int N = 46, R = 214;
  uint16_t lit = m.overLimit ? c565(t.alert) : c565(t.ringFill);
  uint16_t off = c565(t.ringTrack);
  for (int i = 0; i < N; i++) {
    float v = i * m.ringMax / (N - 1);
    int x, y;
    polar(R, ARC_START + ARC_SWEEP * i / (N - 1), x, y);
    bool on = m.fix && m.ringValue >= v && m.ringValue > 0.5f;
    canvas.fillSmoothCircle(x, y, on ? 3 : 2, on ? lit : off);
  }

  char left[24];
  if (!m.gpsConnected) snprintf(left, sizeof(left), "NO GPS");
  else if (!m.fix) snprintf(left, sizeof(left), "SEARCHING %u", m.sats);
  else snprintf(left, sizeof(left), "NIGHT \xC2\xB7 %u%%", m.brightPct);
  statusLine(82, true, left, nullptr, c565(t.status), c565(t.statusIcon), c565(t.status), bg);

  canvas.setFont(&fSpeed);
  canvas.setTextDatum(textdatum_t::baseline_center);
  char sp[8];
  if (m.fix) snprintf(sp, sizeof(sp), "%d", m.speed); else strcpy(sp, "--");
  canvas.setTextColor(m.fix ? (m.overLimit ? c565(t.alert) : c565(t.speed)) : c565(t.status));
  canvas.drawString(sp, CX, 300);

  canvas.setFont(&fLabel);
  canvas.setTextColor(c565(t.unit));
  drawSpaced(m.units, CX, 340, 7);

  canvas.setFont(&fSmall);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(m.toast[0] ? c565(t.ringFill) : c565(t.info));
  canvas.drawString(m.toast[0] ? m.toast : m.clock, CX, 390);
}

// ---------------- Overlays ----------------
static const ThemeColours& activeTheme(const UiModel& m) { return m.night ? settings.night : settings.day; }

static void drawSettings(const UiModel& m) {
  const ThemeColours& t = activeTheme(m);
  canvas.fillScreen(c565(t.background));
  canvas.fillArc(CX, CY, 226, 222, 0, 360, c565(t.ringTrack));
  float frac = m.setCount ? (m.setIndex + 1.0f) / m.setCount : 0;
  canvas.fillArc(CX, CY, 226, 222, 270, 270 + 360 * frac, c565(t.ringFill));

  canvas.setFont(&fSmall);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(c565(t.status));
  char hdr[24];
  snprintf(hdr, sizeof(hdr), "SETTINGS  %d/%d", m.setIndex + 1, m.setCount);
  canvas.drawString(hdr, CX, 96);

  canvas.setFont(&fTitle);
  canvas.setTextColor(c565(t.speed));
  canvas.drawString(m.setTitle, CX, 190);
  canvas.setTextColor(c565(t.ringFill));
  canvas.drawString(m.setValue, CX, 250);

  canvas.setFont(&fSmall);
  canvas.setTextColor(c565(t.info));
  canvas.drawString("A next  \xC2\xB7  B change", CX, 340);
  canvas.drawString("Hold B to save and exit", CX, 372);
}

static void drawStatus(const UiModel& m) {
  const ThemeColours& t = activeTheme(m);
  canvas.fillScreen(c565(t.background));
  canvas.setFont(&fLabel);
  canvas.setTextColor(c565(t.ringFill));
  drawSpaced("STATUS", CX, 72, 5);
  canvas.setFont(&fSmall);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(c565(t.speed));
  int y0 = 122;
  for (int i = 0; i < m.lineCount; i++) canvas.drawString(m.lines[i], CX, y0 + i * 32);
}

static void drawWifiSetup(const UiModel& m) {
  const ThemeColours& t = activeTheme(m);
  canvas.fillScreen(c565(t.background));
  canvas.setFont(&fSmall);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(c565(t.status));
  canvas.drawString("Scan to join setup Wi-Fi", CX, 58);

  // QR code on a white tile so phones read it reliably
  char qr[96];
  snprintf(qr, sizeof(qr), "WIFI:T:WPA;S:%s;P:%s;;", m.apSsid, m.apPass);
  canvas.fillRoundRect(CX - 92, 78, 184, 184, 12, TFT_WHITE);
  canvas.qrcode(qr, CX - 82, 88, 164, 3);

  canvas.setTextColor(c565(t.speed));
  canvas.drawString(m.apSsid, CX, 290);
  char pw[40];
  snprintf(pw, sizeof(pw), "Password %s", m.apPass);
  canvas.drawString(pw, CX, 318);
  canvas.setTextColor(c565(t.ringFill));
  canvas.drawString("Then open http://192.168.4.1", CX, 352);
  canvas.setTextColor(c565(t.info));
  canvas.drawString(m.staLine, CX, 384);
  canvas.drawString("Hold B to close", CX, 412);
}

static void drawUpdate(const UiModel& m) {
  const ThemeColours& t = activeTheme(m);
  canvas.fillScreen(c565(t.background));
  canvas.fillArc(CX, CY, 222, 214, 0, 360, c565(t.ringTrack));
  if (m.otaPct > 0) canvas.fillArc(CX, CY, 222, 214, 270, 270 + 3.6f * m.otaPct, c565(t.ringFill));
  canvas.setFont(&fSmall);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(c565(t.status));
  canvas.drawString("UPDATING FIRMWARE", CX, 96);
  canvas.setFont(&fSpeed);
  canvas.setTextDatum(textdatum_t::baseline_center);
  canvas.setTextColor(c565(t.speed));
  char b[8];
  snprintf(b, sizeof(b), "%d", m.otaPct);
  canvas.drawString(b, CX, 300);
  canvas.setFont(&fLabel);
  canvas.setTextColor(c565(t.unit));
  drawSpaced("%", CX, 342, 0);
  canvas.setFont(&fSmall);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(c565(t.info));
  canvas.drawString("Keep power connected", CX, 392);
}

void uiRender(const UiModel& m) {
  switch (m.overlay) {
    case Overlay::Settings:  drawSettings(m); break;
    case Overlay::Status:    drawStatus(m); break;
    case Overlay::WifiSetup: drawWifiSetup(m); break;
    case Overlay::Update:    drawUpdate(m); break;
    default:
      switch (m.design) {
        case DESIGN_B1: drawGauge(m, false); break;
        case DESIGN_B2: drawGauge(m, true); break;
        case DESIGN_C:  drawInfo(m); break;
        case DESIGN_D:  drawNight(m); break;
        default:        drawDay(m); break;
      }
      break;
  }
  canvas.pushSprite(0, 0);
}
