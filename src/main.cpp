// EX30 GPS speedometer for the M5Stack StopWatch (ESP32-S3, 466x466 AMOLED)
// with the M5Stack GPS/BDS Unit v1.1 (AT6668) on the side port.
//
// Buttons (driving):
//   Key A press        next design: A, B1, B2, C, D
//   Key A hold 1 s     reset trip, max and average
//   Key A double press status page (GPS, Wi-Fi, API, battery)
//   Key B press        brightness nudge: Auto, +1, -1, -2
//   Key B hold 1 s     settings menu
//   A + B hold 2 s     set speed alert to current speed (nearest 10); when stopped, clear it
// Touch zones: centre = status page on/off, top = brighter, bottom = dimmer,
//              right = next design, left = previous design
// In settings: A next item, B change value, hold B save and exit.
// Stopped for 'Dim screen' minutes -> dims; 'Screen off' minutes later -> display off.
// Movement over ~3 mph for 3 s, USB power returning, or any key/touch wakes it.

#include <M5Unified.h>
#include <time.h>
#include <sys/time.h>
#include "settings.h"
#include "gps.h"
#include "speed_filter.h"
#include "brightness.h"
#include "telemetry.h"
#include "net.h"
#include "ui.h"

// M5GFX copies each VLW glyph onto the stack (alloca(w * h)). The largest speed digit is
// 21.9 KB, far more than the default 8 KB loop stack. Size this as the largest glyph plus margin.
SET_LOOP_TASK_STACK_SIZE(32 * 1024);

static SpeedFilter filter;
static StillDetector still;
static Brightness brightness;
static UiModel lastModel;
static Overlay overlay = Overlay::None;
static uint32_t overlayUntil = 0;
static char toast[40] = "";
static uint32_t toastUntil = 0;
static int menuIndex = 0;
static uint8_t lastBrightSent = 0;
static uint8_t appliedHz = 0;
static uint8_t baseRotation = 0;   // board default, read after M5.begin()
static uint8_t appliedRotation = 0xFF;
static bool timeSet = false;
static int batteryPct = 0;
static bool alerted = false;
static bool comboActive = false;
static uint32_t comboStart = 0;
static bool tripDirty = false;

// ---------------- haptics ----------------
static uint16_t hapPattern[6];
static int hapLen = 0, hapPos = 0;
static uint32_t hapNext = 0;

static void vibrate(std::initializer_list<uint16_t> pattern) {
  hapLen = 0;
  for (auto v : pattern) if (hapLen < 6) hapPattern[hapLen++] = v;
  hapPos = 0;
  hapNext = 0;
}
static void hapticTick(uint32_t now) {
  if (hapPos >= hapLen || now < hapNext) return;
  bool on = (hapPos % 2) == 0;          // even entries = on time, odd = gap
  M5.Power.setVibration(on ? 180 : 0);
  hapNext = now + hapPattern[hapPos];
  hapPos++;
}
static void hapticFinish(uint32_t now) {
  if (hapLen && hapPos >= hapLen && now >= hapNext) { M5.Power.setVibration(0); hapLen = 0; }
}

static void showToast(const char* msg, uint32_t ms = 1600) {
  strncpy(toast, msg, sizeof(toast) - 1);
  toast[sizeof(toast) - 1] = 0;
  toastUntil = millis() + ms;
}

// ---------------- time ----------------
static void applyTz() {
  setenv("TZ", settings.tz, 1);
  tzset();
}

static time_t utcFromGps() {
  struct tm t = {};
  t.tm_year = gps.year - 1900;
  t.tm_mon = gps.month - 1;
  t.tm_mday = gps.day;
  t.tm_hour = gps.hour;
  t.tm_min = gps.minute;
  t.tm_sec = gps.second;
  // mktime uses local time; work in UTC by temporarily clearing TZ
  setenv("TZ", "UTC0", 1);
  tzset();
  time_t r = mktime(&t);
  applyTz();
  return r;
}

static void syncClock(uint32_t now) {
  static uint32_t lastSync = 0;
  if (!gps.timeValid || !gps.fix) return;
  if (timeSet && now - lastSync < 60000) return;
  lastSync = now;
  time_t g = utcFromGps();
  time_t sys = time(nullptr);
  if (!timeSet || labs((long)(g - sys)) > 2) {
    struct timeval tv = {g, 0};
    settimeofday(&tv, nullptr);
    timeSet = true;
    struct tm u;
    gmtime_r(&g, &u);
    M5.Rtc.setDateTime(&u);              // RTC kept in UTC
  }
}

static void clockFromRtc() {
  if (!M5.Rtc.isEnabled()) return;
  auto dt = M5.Rtc.getDateTime();
  if (dt.date.year < 2024) return;
  struct tm t = {};
  t.tm_year = dt.date.year - 1900;
  t.tm_mon = dt.date.month - 1;
  t.tm_mday = dt.date.date;
  t.tm_hour = dt.time.hours;
  t.tm_min = dt.time.minutes;
  t.tm_sec = dt.time.seconds;
  setenv("TZ", "UTC0", 1);
  tzset();
  time_t r = mktime(&t);
  applyTz();
  struct timeval tv = {r, 0};
  settimeofday(&tv, nullptr);
  timeSet = true;
}

// ---------------- settings menu ----------------
static const int8_t NUDGES[] = {0, 1, -1, -2};

struct MenuItem { const char* title; void (*value)(char*, size_t); void (*change)(); };

static void vUnits(char* b, size_t n) { snprintf(b, n, "%s", settings.units == Units::MPH ? "mph" : "km/h"); }
static void cUnits() { settings.units = settings.units == Units::MPH ? Units::KMH : Units::MPH; settings.alertLimit = 0; }
static void vDesign(char* b, size_t n) { snprintf(b, n, "%s", designName(settings.design)); }
static void cDesign() { settings.design = (settings.design + 1) % DESIGN_COUNT; }
static void vAutoNight(char* b, size_t n) { snprintf(b, n, "%s", settings.autoNight ? "On (D after dark)" : "Off"); }
static void cAutoNight() { settings.autoNight = !settings.autoNight; }
static void vDayCol(char* b, size_t n) { snprintf(b, n, "%s", dayPresetName(settings.dayPreset)); }
static void cDayCol() { applyDayPreset(settings.dayPreset + 1); }
static void vNightCol(char* b, size_t n) { snprintf(b, n, "%s", nightPresetName(settings.nightPreset)); }
static void cNightCol() { applyNightPreset(settings.nightPreset + 1); }
static void vBright(char* b, size_t n) {
  if (settings.brightMode == BrightMode::AutoSun) snprintf(b, n, "Auto by sun");
  else snprintf(b, n, "Fixed %u%%", brightStepPercent(settings.fixedLevel));
}
static void cBright() {
  if (settings.brightMode == BrightMode::AutoSun) { settings.brightMode = BrightMode::Fixed; settings.fixedLevel = 0; }
  else if (settings.fixedLevel < BRIGHT_STEPS - 1) settings.fixedLevel++;
  else settings.brightMode = BrightMode::AutoSun;
  settings.brightOffset = 0;
}
static void vHz(char* b, size_t n) { snprintf(b, n, "%u Hz", settings.gpsHz); }
static void cHz() { settings.gpsHz = settings.gpsHz == 10 ? 1 : (settings.gpsHz == 1 ? 5 : 10); }
static void vSmooth(char* b, size_t n) { const char* v[] = {"Low", "Medium", "High"}; snprintf(b, n, "%s", v[(int)settings.smoothing]); }
static void cSmooth() { settings.smoothing = (Smoothing)(((int)settings.smoothing + 1) % 3); }
static void vAlert(char* b, size_t n) { const char* v[] = {"Off", "Vibrate", "Vibrate + tone"}; snprintf(b, n, "%s", v[(int)settings.alertMode]); }
static void cAlert() { settings.alertMode = (AlertMode)(((int)settings.alertMode + 1) % 3); }
static void vLimit(char* b, size_t n) {
  if (!settings.alertLimit) snprintf(b, n, "Off");
  else snprintf(b, n, "%u %s", settings.alertLimit, settings.units == Units::MPH ? "mph" : "km/h");
}
static void cLimit() {
  int lo = settings.units == Units::MPH ? 20 : 30, hi = settings.units == Units::MPH ? 80 : 130;
  if (!settings.alertLimit) settings.alertLimit = lo;
  else if (settings.alertLimit + 10 > hi) settings.alertLimit = 0;
  else settings.alertLimit += 10;
}
static void vWifi(char* b, size_t n) {
  if (!settings.wifiEnabled) snprintf(b, n, "Off");
  else if (netConnected()) snprintf(b, n, "On, %s", netSsid().c_str());
  else snprintf(b, n, "On, not connected");
}
static void cWifi() { settings.wifiEnabled = !settings.wifiEnabled; }
static void vSetup(char* b, size_t n) { snprintf(b, n, "Press B to start"); }
static void cSetup() { netStartPortal(); overlay = Overlay::WifiSetup; }
static void vTele(char* b, size_t n) {
  if (!settings.apiUrl[0]) snprintf(b, n, "Set URL on web page");
  else snprintf(b, n, "%s", settings.telemetryEnabled ? "On" : "Off");
}
static void cTele() { settings.telemetryEnabled = !settings.telemetryEnabled; }
static void vIdleDim(char* b, size_t n) {
  if (!settings.idleDimMin) snprintf(b, n, "Never");
  else snprintf(b, n, "After %u min stopped", settings.idleDimMin);
}
static void cIdleDim() {
  static const uint8_t V[] = {0, 1, 2, 3, 5, 10, 15, 30};
  int i = 0;
  for (int k = 0; k < 8; k++) if (V[k] == settings.idleDimMin) i = k;
  settings.idleDimMin = V[(i + 1) % 8];
}
static void vIdleOff(char* b, size_t n) {
  if (!settings.idleOffMin) snprintf(b, n, "Stay dimmed");
  else snprintf(b, n, "%u min after dimming", settings.idleOffMin);
}
static void cIdleOff() {
  static const uint8_t V[] = {0, 1, 2, 5, 10, 30};
  int i = 0;
  for (int k = 0; k < 6; k++) if (V[k] == settings.idleOffMin) i = k;
  settings.idleOffMin = V[(i + 1) % 6];
}
static void resetTrip();
static void vTrip(char* b, size_t n) { snprintf(b, n, "Press B to reset"); }
static void cTrip() { resetTrip(); showToast("Trip reset"); }

static const MenuItem MENU[] = {
  {"Units", vUnits, cUnits},
  {"Design", vDesign, cDesign},
  {"Auto night", vAutoNight, cAutoNight},
  {"Day colours", vDayCol, cDayCol},
  {"Night colours", vNightCol, cNightCol},
  {"Brightness", vBright, cBright},
  {"Dim screen", vIdleDim, cIdleDim},
  {"Screen off", vIdleOff, cIdleOff},
  {"GPS updates", vHz, cHz},
  {"Smoothing", vSmooth, cSmooth},
  {"Speed alert", vAlert, cAlert},
  {"Alert limit", vLimit, cLimit},
  {"Wi-Fi", vWifi, cWifi},
  {"Wi-Fi setup", vSetup, cSetup},
  {"Send to API", vTele, cTele},
  {"Trip", vTrip, cTrip},
};
static constexpr int MENU_COUNT = sizeof(MENU) / sizeof(MENU[0]);

// ---------------- trip ----------------
static void resetTrip() {
  settings.tripMeters = 0;
  settings.tripMovingSec = 0;
  settings.tripMaxMs = 0;
  tripDirty = true;
}

static void applyRotation() {
  if (appliedRotation == settings.rotation) return;
  M5.Display.setRotation((baseRotation + settings.rotation) & 3);
  appliedRotation = settings.rotation;
  memset(&lastModel, 0xFF, sizeof(lastModel));   // force a full redraw
}

static void applySettingsChanges() {
  applyRotation();
  filter.setLevel((uint8_t)settings.smoothing);
  filter.setStartGate(settings.startSpeedMs, settings.startHoldMs);
  if (appliedHz != settings.gpsHz) {
    Serial.printf("[Settings] GPS update rate: %u Hz\n", settings.gpsHz);
    gpsSetRate(settings.gpsHz);
    appliedHz = settings.gpsHz;
  }
  applyTz();
}

// ---------------- inactivity: dim, then screen off ----------------
// The car counts as stopped unless the smoothed speed stays above ~3 mph for 3 s, so GPS
// jitter while parked never resets the timer. Any key, touch, real movement or USB power
// coming on (car switched on) wakes the screen. The first key/touch on a dark screen only wakes it.
enum class Screen : uint8_t { On, Dimmed, Off };
static Screen screen = Screen::On;
static uint32_t lastActivityMs = 0;
static uint32_t movingSince = 0;
static uint32_t swallowUntil = 0;     // ignore input until this time (after a wake-up press)
static float dimFactor = 1.0f;
static bool usbPower = false, charging = false;
static int vbusMv = -1;

static constexpr float MOVING_MS = 1.34f;          // ~3 mph
static constexpr uint32_t MOVING_CONFIRM_MS = 3000;

static void wakeScreen(uint32_t now) {
  lastActivityMs = now;
  if (screen == Screen::Off) {
    M5.Display.wakeup();
    memset(&lastModel, 0xFF, sizeof(lastModel));   // force a full redraw
  }
  screen = Screen::On;
}

static void updatePower(uint32_t now) {
  static uint32_t last = 0;
  if (last && now - last < 2000) return;
  last = now;
  bool wasUsb = usbPower;
  vbusMv = M5.Power.getVBUSVoltage();
  charging = M5.Power.isCharging() == m5::Power_Class::is_charging_t::is_charging;
  usbPower = vbusMv > 4000 || (vbusMv < 0 && charging);
  if (usbPower && !wasUsb && last != 0) wakeScreen(now);   // car switched on
}

static void updateIdle(uint32_t now, float speedMs, bool busy) {
  // Real movement: above the threshold continuously for a few seconds
  if (speedMs > MOVING_MS) {
    if (!movingSince) movingSince = now;
    if (now - movingSince >= MOVING_CONFIRM_MS) wakeScreen(now);
  } else {
    movingSince = 0;
  }
  if (busy || settings.idleDimMin == 0) { if (screen != Screen::On) wakeScreen(now); else lastActivityMs = now; }

  uint32_t idle = now - lastActivityMs;
  uint32_t dimAt = (uint32_t)settings.idleDimMin * 60000UL;
  uint32_t offAt = dimAt + (uint32_t)settings.idleOffMin * 60000UL;
  if (screen == Screen::On && settings.idleDimMin && idle >= dimAt) screen = Screen::Dimmed;
  if (screen == Screen::Dimmed && settings.idleOffMin && idle >= offAt) {
    screen = Screen::Off;
    M5.Display.sleep();
  }

  // Ease the dim level so it fades rather than jumps
  static uint32_t lastMs = 0;
  float dt = lastMs ? (now - lastMs) / 1000.0f : 0;
  lastMs = now;
  float target = screen == Screen::On ? 1.0f : 0.25f;
  float k = target < dimFactor ? dt / 2.0f : dt / 0.3f;   // dim over ~2 s, brighten in ~0.3 s
  dimFactor += (target - dimFactor) * fminf(1.0f, k);
  if (fabsf(target - dimFactor) < 0.01f) dimFactor = target;
}

// Returns true when this loop's input should be ignored (it was used to wake the screen).
static bool inputWakeFilter(uint32_t now) {
  auto t = M5.Touch.getDetail();
  bool pressed = M5.BtnA.isPressed() || M5.BtnB.isPressed() || t.isPressed();
  bool newPress = M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || t.wasPressed();
  if (newPress) {
    if (screen != Screen::On) { wakeScreen(now); swallowUntil = UINT32_MAX; vibrate({20}); }
    else lastActivityMs = now;
  }
  if (swallowUntil == UINT32_MAX && !pressed) swallowUntil = now + 600;  // let click detection expire
  if (pressed) lastActivityMs = now;
  return now < swallowUntil;
}

// ---------------- design selection ----------------
// During the day the chosen design (settings.design) is shown. With Auto night on, D is
// shown after dark; stepping designs at night is a temporary choice that lasts until
// the next change between day and night.
static int8_t nightPick = -1;          // design picked while auto night is active, -1 = D
static bool lastNightState = false;

static bool autoNightActive() { return settings.autoNight && brightness.nightDesign(); }

static uint8_t currentDesign() {
  if (autoNightActive()) return nightPick >= 0 ? nightPick : DESIGN_D;
  return settings.design;
}

static void stepDesign(int dir) {
  uint8_t d = (currentDesign() + DESIGN_COUNT + dir) % DESIGN_COUNT;
  if (autoNightActive()) nightPick = d;
  else { settings.design = d; tripDirty = true; }
  char b[40];
  snprintf(b, sizeof(b), "Design %s", designName(d));
  showToast(b, 1200);
  vibrate({25});
}

static void stepBrightness(int dir) {
  int base = settings.brightMode == BrightMode::Fixed ? settings.fixedLevel : brightness.autoStep();
  int curStep = constrain(base - settings.brightOffset, 0, BRIGHT_STEPS - 1);
  int newStep = constrain(curStep - dir, 0, BRIGHT_STEPS - 1);
  if (newStep == curStep) { showToast(dir > 0 ? "Brightness at maximum" : "Brightness at minimum", 1200); vibrate({20, 60, 20}); return; }
  settings.brightOffset = base - newStep;
  brightness.quick = true;
  tripDirty = true;
  vibrate({25});
}

static void toggleStatus(uint32_t now) {
  if (overlay == Overlay::Status) overlay = Overlay::None;
  else { overlay = Overlay::Status; overlayUntil = now + 8000; }
}

// Five touch zones on the round screen: centre circle, and N / E / S / W quarters around it.
enum class Zone { None, Centre, North, East, South, West };
static Zone touchZone(int x, int y) {
  int dx = x - 233, dy = y - 233;
  if (dx * dx + dy * dy < 95 * 95) return Zone::Centre;
  if (abs(dy) >= abs(dx)) return dy < 0 ? Zone::North : Zone::South;
  return dx > 0 ? Zone::East : Zone::West;
}

// ---------------- buttons ----------------
static void handleButtons(uint32_t now, float speedUnits) {
  auto& A = M5.BtnA;
  auto& B = M5.BtnB;

  // Combination: both keys held together
  if (A.isPressed() && B.isPressed()) {
    if (!comboActive) { comboActive = true; comboStart = now; }
    if (comboStart && now - comboStart > 2000 && overlay == Overlay::None) {
      comboStart = 0;  // fire once
      if (speedUnits < 5) { settings.alertLimit = 0; showToast("Speed alert off"); }
      else {
        settings.alertLimit = (uint8_t)(lroundf(speedUnits / 10.0f) * 10);
        char b[40];
        snprintf(b, sizeof(b), "Alert at %u %s", settings.alertLimit, settings.units == Units::MPH ? "mph" : "km/h");
        showToast(b, 2200);
      }
      alerted = false;
      settingsSave();
      vibrate({60, 80, 60});
    }
    return;
  }
  if (comboActive) {
    if (!A.isPressed() && !B.isPressed()) comboActive = false;
    return;  // swallow the events from the combination
  }

  if (overlay == Overlay::Settings) {
    if (A.wasSingleClicked() || A.wasDoubleClicked()) { menuIndex = (menuIndex + 1) % MENU_COUNT; vibrate({25}); }
    if (B.wasSingleClicked() || B.wasDoubleClicked()) { MENU[menuIndex].change(); applySettingsChanges(); vibrate({25}); }
    if (B.wasHold()) {
      settingsSave();
      applySettingsChanges();
      if (overlay == Overlay::Settings) overlay = Overlay::None;
      showToast("Settings saved");
      vibrate({50, 70, 50});
    }
    return;
  }
  if (overlay == Overlay::WifiSetup) {
    if (B.wasHold() || A.wasHold()) { netStopPortal(); overlay = Overlay::None; vibrate({40}); }
    return;
  }
  if (overlay == Overlay::Status) {
    // Close on a completed press so the same press does not also act on the driving screen
    if (A.wasSingleClicked() || A.wasDoubleClicked() || A.wasHold() ||
        B.wasSingleClicked() || B.wasDoubleClicked() || B.wasHold()) {
      overlay = Overlay::None;
    }
    return;
  }

  // Driving screen
  if (A.wasSingleClicked()) stepDesign(+1);
  if (A.wasDoubleClicked()) { overlay = Overlay::Status; overlayUntil = now + 8000; }
  if (A.wasHold()) { resetTrip(); showToast("Trip reset"); vibrate({60, 80, 60}); }

  if (B.wasSingleClicked() || B.wasDoubleClicked()) {
    int idx = 0;
    for (int i = 0; i < 4; i++) if (NUDGES[i] == settings.brightOffset) idx = i;
    settings.brightOffset = NUDGES[(idx + 1) % 4];
    brightness.quick = true;
    tripDirty = true;  // saved with the next periodic save
    vibrate({25});
  }
  if (B.wasHold()) { overlay = Overlay::Settings; menuIndex = 0; vibrate({40}); }

}

static void handleTouch(uint32_t now) {
  if (overlay == Overlay::Settings || overlay == Overlay::WifiSetup || netOtaProgress >= 0) return;
  auto t = M5.Touch.getDetail();
  if (!t.wasClicked()) return;
  Zone z = touchZone(t.x, t.y);
  if (overlay == Overlay::Status) {           // any tap closes the status page
    overlay = Overlay::None;
    vibrate({20});
    return;
  }
  switch (z) {
    case Zone::Centre: toggleStatus(now); vibrate({20}); break;
    case Zone::North:  stepBrightness(+1); break;
    case Zone::South:  stepBrightness(-1); break;
    case Zone::East:   stepDesign(+1); break;
    case Zone::West:   stepDesign(-1); break;
    default: break;
  }
}

// ---------------- USB serial console ----------------
// A fallback for entering Wi-Fi details from a PC (115200 baud, any serial monitor):
//   wifi1 "Phone hotspot name" "password"     Network 1 (phone hotspot)
//   wifi2 "Home network" "password"           Network 2 (home)
//   clear1 / clear2   status   ota "new-password"   reboot   help
static int splitArgs(char* line, char* argv[], int max) {
  int n = 0;
  char* p = line;
  while (*p && n < max) {
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) break;
    if (*p == '"') {
      argv[n++] = ++p;
      while (*p && *p != '"') p++;
    } else {
      argv[n++] = p;
      while (*p && *p != ' ' && *p != '\t') p++;
    }
    if (*p) *p++ = 0;
  }
  return n;
}

static void serialConsole() {
  static char buf[200];
  static size_t len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') { if (len < sizeof(buf) - 1) buf[len++] = c; continue; }
    buf[len] = 0;
    len = 0;
    char* argv[4];
    int argc = splitArgs(buf, argv, 4);
    if (!argc) continue;
    String cmd = argv[0];
    cmd.toLowerCase();
    if ((cmd == "wifi1" || cmd == "wifi2") && argc >= 2) {
      int slot = cmd == "wifi1" ? 0 : 1;
      strncpy(settings.wifi[slot].ssid, argv[1], sizeof(settings.wifi[slot].ssid) - 1);
      settings.wifi[slot].ssid[sizeof(settings.wifi[slot].ssid) - 1] = 0;
      strncpy(settings.wifi[slot].pass, argc >= 3 ? argv[2] : "", sizeof(settings.wifi[slot].pass) - 1);
      settings.wifi[slot].pass[sizeof(settings.wifi[slot].pass) - 1] = 0;
      settings.wifiEnabled = true;
      settingsSave();
      Serial.printf("Network %d saved: \"%s\"\n", slot + 1, settings.wifi[slot].ssid);
    } else if (cmd == "clear1" || cmd == "clear2") {
      int slot = cmd == "clear1" ? 0 : 1;
      memset(&settings.wifi[slot], 0, sizeof(WifiCred));
      settingsSave();
      Serial.printf("Network %d cleared\n", slot + 1);
    } else if (cmd == "ota" && argc >= 2 && strlen(argv[1]) >= 6) {
      strncpy(settings.otaPass, argv[1], sizeof(settings.otaPass) - 1);
      settingsSave();
      Serial.println("OTA password saved (takes effect after reboot)");
    } else if (cmd == "status") {
      for (int i = 0; i < MAX_WIFI; i++)
        Serial.printf("Network %d: \"%s\" %s\n", i + 1, settings.wifi[i].ssid, settings.wifi[i].pass[0] ? "(password set)" : "(no password)");
      Serial.printf("Wi-Fi: %s %s %s\n", netConnected() ? "connected to" : "not connected",
                    netConnected() ? netSsid().c_str() : "", netConnected() ? netIp().c_str() : "");
      Serial.printf("Hostname: %s.local\n", settings.deviceName);
      Serial.printf("GPS: %s, %u sats, %.1f Hz, RX G%d @ %lu\n", gps.fix ? "fix" : "no fix", gps.sats, gps.measuredHz, gps.rxPin, (unsigned long)gps.baud);
    } else if (cmd == "reboot") {
      Serial.println("Restarting");
      delay(200);
      ESP.restart();
    } else {
      Serial.println("Commands: wifi1 \"name\" \"pass\" | wifi2 \"name\" \"pass\" | clear1 | clear2 | ota \"pass\" | status | reboot");
    }
  }
}

// ---------------- setup / loop ----------------
void setup() {
  auto cfg = M5.config();
  cfg.output_power = true;       // 5 V out to the side port for the GPS unit
  cfg.internal_imu = true;
  cfg.internal_rtc = true;
  cfg.internal_spk = true;
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);
  M5.Power.setExtOutput(true);   // make sure the port 5 V is on
  M5.Display.setBrightness(255);
  baseRotation = M5.Display.getRotation();
  M5.BtnA.setHoldThresh(1000);
  M5.BtnB.setHoldThresh(1000);
  Serial.println("\n[Boot] EX30 speedometer starting");

  settingsLoad();
  applyTz();
  clockFromRtc();
  Serial.printf("[Boot] Settings loaded: %s, GPS %u Hz, Wi-Fi %s\n",
                settings.units == Units::MPH ? "mph" : "km/h", settings.gpsHz,
                settings.wifiEnabled ? "enabled" : "disabled");

  applyRotation();
  uiBegin();
  UiModel splash;
  memset(&splash, 0, sizeof(splash));
  splash.units = unitsLabel();
  strcpy(splash.clock, "--:--");
  strcpy(splash.toast, "Starting GPS...");
  uiRender(splash);

  Serial.println("[Boot] Detecting GPS receiver...");
  gpsBegin();
  Serial.printf("[Boot] GPS listening on RX G%d at %lu baud\n", gps.rxPin, (unsigned long)gps.baud);
  appliedHz = settings.gpsHz;
  filter.setLevel((uint8_t)settings.smoothing);
  filter.setStartGate(settings.startSpeedMs, settings.startHoldMs);

  telemetryBegin();
  netBegin();
  Serial.println("[Boot] Network and telemetry tasks started");
  Serial.println("[Boot] Send 'status' for details; type 'help' for serial commands");
  memset(&lastModel, 0xFF, sizeof(lastModel));
  lastActivityMs = millis();
}

void loop() {
  M5.update();
  uint32_t now = millis();

  // --- sensors ---
  static uint32_t lastImu = 0;
  if (M5.Imu.isEnabled() && now - lastImu >= 20) {
    lastImu = now;
    float ax, ay, az;
    if (M5.Imu.getAccel(&ax, &ay, &az)) still.update(ax, ay, az, now);
  }

  bool newSample = gpsPoll();
  // A fresh fix reports a few mph of speed while standing still. Ignore the speed until the
  // fix has settled.
  static uint32_t fixSinceMs = 0;
  if (!gps.fix) fixSinceMs = 0;
  else if (!fixSinceMs) fixSinceMs = now;
  if (newSample) {
    static uint32_t lastSampleMs = 0;
    bool trusted = now - fixSinceMs >= (uint32_t)settings.fixSettleSec * 1000;
    filter.addSample(trusted ? gps.speedMs : 0, now, still.available && still.still());
    float dt = lastSampleMs ? (now - lastSampleMs) / 1000.0f : 0;
    lastSampleMs = now;
    float v = filter.filteredMs();
    if (dt > 0 && dt < 2.0f && v > 0.5f) {
      settings.tripMeters += v * dt;
      settings.tripMovingSec += dt;
      if (v > settings.tripMaxMs) settings.tripMaxMs = v;
      tripDirty = true;
    }
  }
  if (!gps.fix) filter.reset();
  filter.tick(now);
  syncClock(now);

  serialConsole();
  if (netSettingsChanged) { netSettingsChanged = false; applySettingsChanges(); }
  if (netTripResetRequested) { netTripResetRequested = false; resetTrip(); showToast("Trip reset"); }

  float unitsPerMs = unitsFromMs(1.0f);
  int speedInt = filter.displayInt(unitsPerMs);
  if (speedInt > 0) speedInt = max(1, speedInt + settings.speedOffset);  // user calibration
  float speedUnits = filter.filteredMs() * unitsPerMs;

  static uint32_t lastDebug = 0;
  if (now - lastDebug >= 5000) {
    lastDebug = now;
    Serial.printf("[Status] GPS %s, fix %s, sats %u, HDOP %.1f, %.1f Hz, speed %.1f %s (raw %.1f) | Wi-Fi %s | screen %s | clock %s\n",
                  gps.connected ? "data" : "silent", gps.fix ? "yes" : "no", gps.sats, gps.hdop,
                  gps.measuredHz, speedUnits, unitsLabel(), unitsFromMs(gps.speedMs),
                  netConnected() ? netIp().c_str() : "disconnected",
                  screen == Screen::On ? "on" : (screen == Screen::Dimmed ? "dimmed" : "off"),
                  timeSet ? "synced" : "waiting");
  }

  updatePower(now);
  bool busy = overlay == Overlay::Settings || overlay == Overlay::WifiSetup || netOtaProgress >= 0;
  updateIdle(now, filter.filteredMs(), busy);
  if (!inputWakeFilter(now)) {
    handleButtons(now, speedUnits);
    handleTouch(now);
  }
  hapticTick(now);
  hapticFinish(now);

  // --- brightness / design ---
  time_t utc = time(nullptr);
  float lat = gps.fix ? gps.lat : settings.lastLat;
  float lon = gps.fix ? gps.lon : settings.lastLon;
  brightness.update(utc, timeSet, lat, lon, now);
  uint8_t wantLevel = (uint8_t)max(4.0f, brightness.level() * dimFactor);
  if (screen != Screen::Off && wantLevel != lastBrightSent) {
    lastBrightSent = wantLevel;
    M5.Display.setBrightness(lastBrightSent);
  }
  if (brightness.nightDesign() != lastNightState) { lastNightState = brightness.nightDesign(); nightPick = -1; }
  uint8_t design = currentDesign();
  bool night = design == DESIGN_D;

  // --- speed alert ---
  bool over = settings.alertLimit && gps.fix && speedInt > settings.alertLimit;
  if (over && !alerted && settings.alertMode != AlertMode::Off) {
    alerted = true;
    vibrate({120, 100, 120});
    if (settings.alertMode == AlertMode::HapticTone) M5.Speaker.tone(2200, 160);
  }
  if (!over && speedInt < (int)settings.alertLimit - 1) alerted = false;

  // --- periodic jobs ---
  static uint32_t lastBattery = 0, lastSave = 0, lastSample = 0;
  if (now - lastBattery > 10000 || lastBattery == 0) { lastBattery = now; batteryPct = M5.Power.getBatteryLevel(); }
  if (gps.fix) { settings.lastLat = gps.lat; settings.lastLon = gps.lon; }
  if (tripDirty && now - lastSave > 120000) { lastSave = now; tripDirty = false; settingsSaveTrip(); }

  if (settings.telemetryEnabled && gps.fix && timeSet && now - lastSample >= (uint32_t)settings.sampleSec * 1000) {
    lastSample = now;
    TelemetryPoint p;
    p.utc = (uint32_t)utc;
    p.lat = gps.lat;
    p.lon = gps.lon;
    p.speedMs = filter.filteredMs();
    p.courseDeg = gps.courseDeg;
    p.altM = gps.altM;
    p.hdop = gps.hdop;
    p.sats = gps.sats;
    telemetryRecord(p);
  }

  liveData.speed = speedInt;
  liveData.sats = gps.sats;
  liveData.fix = gps.fix;
  liveData.hz = gps.measuredHz;
  liveData.brightnessPct = brightness.percent();
  liveData.night = night;
  liveData.battery = batteryPct;
  liveData.usbPower = usbPower;
  liveData.charging = charging;
  liveData.vbusMv = vbusMv;
  liveData.screen = (uint8_t)screen;

  if (overlay == Overlay::Status && now > overlayUntil) overlay = Overlay::None;

  // --- build the frame ---
  if (screen == Screen::Off) { delay(20); return; }   // nothing to draw; GPS, Wi-Fi and API keep running
  static uint32_t lastFrame = 0;
  if (now - lastFrame < 33) { delay(1); return; }  // ~30 fps cap
  lastFrame = now;

  UiModel m;
  memset(&m, 0, sizeof(m));
  m.design = design;
  m.night = night;
  m.limit = settings.alertLimit;
  m.hdop = gps.hdop;
  if (filter.filteredMs() > 1.5f) {
    static const char* CARD[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int c = (int)((gps.courseDeg + 22.5f) / 45.0f) % 8;
    snprintf(m.heading, sizeof(m.heading), "%s %03d\xC2\xB0", CARD[c], (int)lroundf(gps.courseDeg) % 360);
  } else {
    strcpy(m.heading, "--");
  }
  m.gpsConnected = gps.connected;
  m.fix = gps.fix;
  m.speed = speedInt;
  m.ringValue = roundf(filter.displayMs() * unitsPerMs * 4) / 4;   // quarter-unit steps: smooth but not every frame
  m.ringMax = settings.units == Units::MPH ? 120 : 200;
  m.overLimit = over;
  m.sats = gps.sats;
  m.hz = roundf(gps.measuredHz);
  m.brightPct = brightness.percent();
  m.units = unitsLabel();
  if (timeSet) {
    time_t lt = time(nullptr);
    struct tm tmv;
    localtime_r(&lt, &tmv);
    strftime(m.clock, sizeof(m.clock), "%H:%M", &tmv);
  } else {
    strcpy(m.clock, "--:--");
  }
  float tripU = settings.units == Units::MPH ? settings.tripMeters / 1609.344f : settings.tripMeters / 1000.0f;
  snprintf(m.trip, sizeof(m.trip), tripU < 100 ? "%.1f %s" : "%.0f %s", tripU, settings.units == Units::MPH ? "mi" : "km");
  snprintf(m.tripVal, sizeof(m.tripVal), tripU < 100 ? "%.1f" : "%.0f", tripU);
  strcpy(m.tripUnit, settings.units == Units::MPH ? "mi" : "km");
  snprintf(m.maxVal, sizeof(m.maxVal), "%.0f", settings.tripMaxMs * unitsPerMs);
  snprintf(m.avgVal, sizeof(m.avgVal), "%.0f", settings.tripMovingSec > 1 ? settings.tripMeters / settings.tripMovingSec * unitsPerMs : 0.0f);
  if (gps.fix) snprintf(m.altVal, sizeof(m.altVal), "%.0f", gps.altM); else strcpy(m.altVal, "--");
  if (now < toastUntil) {
    strncpy(m.toast, toast, sizeof(m.toast) - 1);
  }
  // Brightness nudge feedback
  static int8_t lastOffset = settings.brightOffset;
  if (settings.brightOffset != lastOffset) {
    lastOffset = settings.brightOffset;
    char b[40];
    if (settings.brightOffset == 0) snprintf(b, sizeof(b), "Brightness auto %u%%", brightness.percent());
    else snprintf(b, sizeof(b), "Brightness %u%% (%+d)", brightness.percent(), settings.brightOffset);
    showToast(b);
    strncpy(m.toast, toast, sizeof(m.toast) - 1);
  }

  m.overlay = overlay;
  if (netOtaProgress >= 0) { m.overlay = Overlay::Update; m.otaPct = netOtaProgress; }
  if (overlay == Overlay::Settings) {
    m.setIndex = menuIndex;
    m.setCount = MENU_COUNT;
    strncpy(m.setTitle, MENU[menuIndex].title, sizeof(m.setTitle) - 1);
    MENU[menuIndex].value(m.setValue, sizeof(m.setValue));
  } else if (overlay == Overlay::Status) {
    auto t = telemetryStats();
    int i = 0;
    snprintf(m.lines[i++], 48, gps.fix ? "GPS fix \xC2\xB7 %u sats \xC2\xB7 HDOP %.1f" : "GPS no fix \xC2\xB7 %u sats \xC2\xB7 HDOP %.1f", gps.sats, gps.hdop);
    snprintf(m.lines[i++], 48, "%.1f Hz \xC2\xB7 RX G%d \xC2\xB7 %lu baud", gps.measuredHz, gps.rxPin, (unsigned long)gps.baud);
    if (netConnected()) {
      String ss = netSsid();
      int slot = ss == settings.wifi[1].ssid ? 2 : 1;
      snprintf(m.lines[i++], 48, "Wi-Fi %d: %s", slot, ss.c_str());
    }
    else snprintf(m.lines[i++], 48, "Wi-Fi %s", settings.wifiEnabled ? "searching" : "off");
    if (netConnected()) snprintf(m.lines[i++], 48, "http://%s", netIp().c_str());
    snprintf(m.lines[i++], 48, "API %s \xC2\xB7 queued %lu \xC2\xB7 HTTP %d", settings.telemetryEnabled ? "on" : "off", (unsigned long)t.queued, t.lastHttpCode);
    snprintf(m.lines[i++], 48, "Sun %.1f\xC2\xB0 \xC2\xB7 %s %u%%", brightness.sunDeg(), brightStepName(brightness.step()), brightness.percent());
    float maxU = settings.tripMaxMs * unitsPerMs;
    float avgU = settings.tripMovingSec > 1 ? settings.tripMeters / settings.tripMovingSec * unitsPerMs : 0;
    snprintf(m.lines[i++], 48, "Trip %s \xC2\xB7 max %.0f \xC2\xB7 avg %.0f", m.trip, maxU, avgU);
    if (usbPower) snprintf(m.lines[i++], 48, "Battery %d%% \xC2\xB7 USB %.1f V%s", batteryPct, vbusMv / 1000.0f, charging ? " \xC2\xB7 charging" : "");
    else snprintf(m.lines[i++], 48, "Battery %d%% \xC2\xB7 on battery", batteryPct);
    m.lineCount = i;
  } else if (overlay == Overlay::WifiSetup) {
    strncpy(m.apSsid, netPortalActive() ? netPortalSsid() : "Starting...", sizeof(m.apSsid) - 1);
    strncpy(m.apPass, netPortalActive() ? netPortalPass() : "", sizeof(m.apPass) - 1);
    if (netConnected()) snprintf(m.staLine, sizeof(m.staLine), "Connected to %s", netSsid().c_str());
    else snprintf(m.staLine, sizeof(m.staLine), "Not connected yet");
  }

  if (memcmp(&m, &lastModel, sizeof(m)) != 0) {
    uiRender(m);
    lastModel = m;
  }
}
