/*
 * sim_provider.cpp - synthetic data source implementation.
 * See sim_provider.h for the overview.
 */

#include "sim_provider.h"
#include <math.h>

static inline float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

bool SimProvider::begin() {
  _d = VehicleData{};             // zero-init
  _d.ecuConnected = true;
  _d.ambientKpa   = 101.0f;
  _d.coolantC     = 20.0f;
  _d.batteryMv    = 12600;
  return true;
}

void SimProvider::poll() {
  const float t = millis() / 1000.0f;    // seconds since boot

  // Engine speed: idle ~800, sweeping up towards ~2500 rpm.
  float revFrac = 0.5f + 0.5f * sinf(t * 0.30f);           // 0..1
  _d.rpm = (uint16_t)lroundf(800.0f + revFrac * 1700.0f);

  // Driver demand tracks the rev sweep.
  _d.throttlePct = clampf(5.0f + revFrac * 80.0f, 0.0f, 100.0f);
  _d.loadPct     = clampf(15.0f + (_d.throttlePct / 100.0f) * 70.0f, 0.0f, 100.0f);

  // Road speed: independent smooth cycle 0..110 km/h.
  float spdFrac = 0.5f + 0.5f * sinf(t * 0.15f + 1.0f);
  _d.speedKmh = (uint16_t)lroundf(spdFrac * 110.0f);

  // Temperatures: coolant warms up then holds; others gently vary.
  _d.coolantC    = clampf(20.0f + t * 0.6f, 20.0f, 88.0f) + sinf(t * 0.05f) * 1.5f;
  _d.intakeAirC  = 25.0f + sinf(t * 0.10f) * 8.0f;
  _d.fuelTempC   = clampf(30.0f + t * 0.10f, 30.0f, 55.0f);

  // Air / pressures.
  _d.ambientKpa  = 101.0f + sinf(t * 0.02f) * 1.0f;
  _d.mapKpa      = clampf(100.0f + (_d.throttlePct / 100.0f) * 130.0f, 95.0f, 250.0f);
  _d.boostBar    = (_d.mapKpa - _d.ambientKpa) / 100.0f;
  _d.mafGs       = clampf(5.0f + (_d.rpm / 2500.0f) * 45.0f *
                          (0.3f + 0.7f * _d.throttlePct / 100.0f), 0.0f, 80.0f);

  // Fuelling.
  _d.injectionMg   = clampf(6.0f + (_d.throttlePct / 100.0f) * 45.0f, 0.0f, 80.0f);
  _d.torqueLimitMg = 60.0f;
  _d.smokeLimitMg  = 58.0f;
  _d.idleDemandMg  = 8.0f;

  // Electrical / actuators.
  _d.batteryMv     = (uint16_t)lroundf(14200.0f + sinf(t * 0.20f) * 80.0f);
  _d.egrPct        = clampf(40.0f * (1.0f - _d.throttlePct / 100.0f), 0.0f, 100.0f);
  _d.wastegatePct  = clampf((_d.boostBar / 1.3f) * 100.0f, 0.0f, 100.0f);
  _d.accelTrack1V  = clampf(0.5f + (_d.throttlePct / 100.0f) * 4.0f, 0.0f, 5.0f);
  _d.accelTrack2V  = clampf(5.0f - _d.accelTrack1V, 0.0f, 5.0f);

  // Discrete inputs / gear (derived from speed + time so they visibly change).
  if      (_d.speedKmh < 3)   _d.gear = 0;
  else if (_d.speedKmh < 20)  _d.gear = 1;
  else if (_d.speedKmh < 40)  _d.gear = 2;
  else if (_d.speedKmh < 60)  _d.gear = 3;
  else if (_d.speedKmh < 85)  _d.gear = 4;
  else                        _d.gear = 5;

  int ti = (int)t;
  _d.brakePressed  = (ti % 20) < 2;
  _d.clutchPressed = (ti % 25) < 2;
  _d.cruiseMaster  = _d.speedKmh > 40;
  _d.cruiseSet     = _d.speedKmh > 70;
  _d.cruiseResume  = false;
  _d.acRequest     = (ti % 30) < 15;
  _d.transferHigh  = true;
  _d.accelTrack3V  = _d.accelTrack2V;
  for (int c = 0; c < 5; c++) _d.injTrim[c] = (int16_t)lroundf(sinf(t * 0.4f + c) * 4.0f);

  _d.runtimeSec = (uint32_t)t;
}

int SimProvider::readDTCs() {
  if (!_faultsPresent) {
    _d.dtcCount = 0;
    return 0;
  }
  // Two demo faults:
  //  - one that maps to a genuine standard P-code (Torque shows its own text)
  //  - one Land-Rover-specific injector code (P1668)
  // rawIndex is kept consistent with X-Y: rawIndex = (X-1)*8 + (Y-1).
  _d.dtcs[0] = { 42,  6, 3, 'P', 0x0115, "Engine coolant temp sensor circuit" };
  _d.dtcs[1] = { 90, 12, 3, 'P', 0x1668, "Injector cylinder circuit (Td5)" };
  _d.dtcCount = 2;
  return _d.dtcCount;
}

bool SimProvider::clearDTCs() {
  _faultsPresent = false;
  _d.dtcCount = 0;
  return true;
}
