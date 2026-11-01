#include "brightness.h"
#include "settings.h"
#include <math.h>

static const char* STEP_NAMES[BRIGHT_STEPS] = {"Day", "Low sun", "Dusk", "Twilight", "Night"};
static const uint8_t STEP_PCT[BRIGHT_STEPS] = {100, 75, 50, 30, 15};

const char* brightStepName(int s) { return STEP_NAMES[constrain(s, 0, BRIGHT_STEPS - 1)]; }
uint8_t brightStepPercent(int s) { return STEP_PCT[constrain(s, 0, BRIGHT_STEPS - 1)]; }

float solarElevationDeg(time_t utc, float lat, float lon) {
  struct tm t;
  gmtime_r(&utc, &t);
  float hour = t.tm_hour + t.tm_min / 60.0f + t.tm_sec / 3600.0f;
  float g = 2.0f * (float)M_PI / 365.0f * (t.tm_yday + (hour - 12.0f) / 24.0f);
  float eqtime = 229.18f * (0.000075f + 0.001868f * cosf(g) - 0.032077f * sinf(g)
                            - 0.014615f * cosf(2 * g) - 0.040849f * sinf(2 * g));
  float decl = 0.006918f - 0.399912f * cosf(g) + 0.070257f * sinf(g) - 0.006758f * cosf(2 * g)
               + 0.000907f * sinf(2 * g) - 0.002697f * cosf(3 * g) + 0.00148f * sinf(3 * g);
  float tst = hour * 60.0f + eqtime + 4.0f * lon;          // true solar time, minutes
  float ha = (tst / 4.0f - 180.0f) * (float)M_PI / 180.0f;  // hour angle
  float la = lat * (float)M_PI / 180.0f;
  float cz = sinf(la) * sinf(decl) + cosf(la) * cosf(decl) * cosf(ha);
  cz = fmaxf(-1.0f, fminf(1.0f, cz));
  return 90.0f - acosf(cz) * 180.0f / (float)M_PI;
}

static int stepForSun(float e) {
  if (e > 6) return 0;     // daylight
  if (e > 0) return 1;     // low sun
  if (e > -6) return 2;    // civil twilight
  if (e > -12) return 3;   // nautical twilight
  return 4;                // night
}

void Brightness::update(time_t utc, bool timeValid, float lat, float lon, uint32_t now) {
  if (now - _lastSunCalcMs > 10000 || _lastSunCalcMs == 0) {
    _lastSunCalcMs = now;
    if (timeValid) {
      _sun = solarElevationDeg(utc, lat, lon);
      _autoStep = stepForSun(_sun);
    } else {
      _sun = 45;     // no clock yet: assume day
      _autoStep = 0;
    }
    _night = timeValid && _sun < settings.nightBelowDeg;
  }

  int base = settings.brightMode == BrightMode::Fixed ? settings.fixedLevel : _autoStep;
  // Positive nudge = brighter = lower step number
  _step = constrain(base - settings.brightOffset, 0, BRIGHT_STEPS - 1);

  float target = 255.0f * brightStepPercent(_step) / 100.0f;
  float dt = _lastMs ? (now - _lastMs) / 1000.0f : 0;
  _lastMs = now;
  // Fade: a full 0..255 sweep takes about 5 s, or 0.5 s straight after a button nudge
  float maxDelta = (quick ? 510.0f : 51.0f) * dt;
  if (fabsf(target - _level) <= maxDelta) { _level = target; quick = false; }
  else _level += (target > _level ? maxDelta : -maxDelta);
}
