#pragma once
#include <Arduino.h>

struct GpsState {
  bool connected = false;     // NMEA sentences arriving
  bool fix = false;           // valid position and speed
  double lat = 0, lon = 0;
  float speedMs = 0;          // ground speed from the receiver (Doppler), m/s
  float courseDeg = 0;
  float altM = 0;
  uint8_t sats = 0;
  float hdop = 99;
  float measuredHz = 0;       // RMC sentences per second actually received
  uint32_t lastFixMs = 0;     // millis() of last valid speed sample
  bool timeValid = false;
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;  // UTC
  int rxPin = -1, txPin = -1;
  uint32_t baud = 0;
};

extern GpsState gps;

void gpsBegin();                 // detect pins/baud, configure receiver
bool gpsPoll();                  // feed parser; true when a new speed sample arrived
void gpsSetRate(uint8_t hz);     // 1, 5 or 10 Hz
