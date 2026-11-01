#pragma once
#include <Arduino.h>

// One recorded point. Sent to the remote API as JSON (see README for the format).
struct TelemetryPoint {
  uint32_t utc;       // unix seconds
  double lat, lon;
  float speedMs;
  float courseDeg;
  float altM;
  float hdop;
  uint8_t sats;
};

struct TelemetryStats {
  uint32_t queued = 0;
  uint32_t sent = 0;
  uint32_t dropped = 0;
  int lastHttpCode = 0;
  uint32_t lastSendMs = 0;
};

void telemetryBegin();                       // starts the upload task (core 0)
void telemetryRecord(const TelemetryPoint& p);
TelemetryStats telemetryStats();
