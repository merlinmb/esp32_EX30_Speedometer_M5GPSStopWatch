#pragma once
#include <Arduino.h>
#include <time.h>

// Sun elevation in degrees for a UTC time and position (NOAA approximation, ~0.5 deg).
float solarElevationDeg(time_t utc, float latDeg, float lonDeg);

// Brightness steps. 0 = brightest.
static constexpr int BRIGHT_STEPS = 5;
const char* brightStepName(int step);
uint8_t brightStepPercent(int step);

class Brightness {
 public:
  // Work out the target step from the sun (or fixed setting) plus the user nudge.
  void update(time_t utc, bool timeValid, float lat, float lon, uint32_t nowMs);
  int step() const { return _step; }             // final step after nudge
  int autoStep() const { return _autoStep; }     // step from the sun alone
  float sunDeg() const { return _sun; }
  uint8_t percent() const { return brightStepPercent(_step); }
  bool nightDesign() const { return _night; }    // sun low enough for the night design
  uint8_t level() const { return (uint8_t)_level; }  // current faded 0-255 value
  bool quick = false;   // set after a button nudge for a fast fade

 private:
  int _step = 0, _autoStep = 0;
  float _sun = 45;
  float _level = 255;
  bool _night = false;
  uint32_t _lastMs = 0;
  uint32_t _lastSunCalcMs = 0;
};
