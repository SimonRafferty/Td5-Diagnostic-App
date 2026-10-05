/*
 * fuel_economy.h - rolling fuel-economy estimator for the Td5 diagnostic dongle.
 *
 * Integrates fuel volume (from PID 0x1D injection quantity + PID 0x09 RPM) against
 * distance (from PID 0x0D road speed) to produce instantaneous and last-50-mile
 * average economy. The distance-bucketed window and a lifetime trip-fuel total are
 * persisted to NVS (Preferences) so the average survives ignition/power cycles.
 *
 * Like a car's trip computer, the average only accumulates while the vehicle is
 * MOVING: fuel burnt while stationary (idling in traffic / parked) is left out of
 * the average and the displayed average is frozen until it moves again. Trip fuel
 * used still counts it, because that fuel really was burnt.
 *
 * Assumptions (calibratable constants below - validated against realistic idle
 * ~1.1 L/h and ~37 mpg cruise; tune if the car's trip computer disagrees):
 *   - 5-cylinder 4-stroke  -> 2.5 injection events per engine revolution
 *   - injection quantity is mg per injection event (matches Nanocom "injection qty")
 *   - diesel density 0.832 kg/L
 *   - MPG is IMPERIAL (UK gallon = 4.54609 L)
 */
#ifndef TD5_FUEL_ECONOMY_H
#define TD5_FUEL_ECONOMY_H

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

class FuelEconomy {
public:
  void begin() {
    _prefs.begin("fueleco", false);
    size_t len = _prefs.getBytesLength(KEY);
    if (len == sizeof(_s)) {
      _prefs.getBytes(KEY, &_s, sizeof(_s));
      if (_s.magic != MAGIC) reset();
    } else if (len == sizeof(OldState)) {
      migrateOld();                                // keep the old 10-mile history
    } else {
      reset();
    }
    _lastMs = 0; _lastSaveMs = 0; _instLps = 0; _instMph = 0;
    computeAverages();                             // show the stored average at once
  }

  // Call regularly with the latest decoded values (dt derived from millis()).
  void update(float speedKmh, uint16_t rpm, float injectionMg, uint32_t nowMs) {
    if (_lastMs == 0) { _lastMs = nowMs; return; }
    float dt = (nowMs - _lastMs) / 1000.0f;
    _lastMs = nowMs;
    if (dt <= 0.0f || dt > 5.0f) return;         // skip first sample / long stalls

    float mph = speedKmh * KMH_TO_MPH;
    float lps = (injectionMg * (rpm / 60.0f) * INJ_PER_REV) / 1000.0f / DIESEL_G_PER_L; // mg/s->g/s->L/s
    if (lps < 0.0f) lps = 0.0f;

    _instLps = _instLps * 0.9f + lps * 0.1f;     // smoothed for the instantaneous readout
    _instMph = _instMph * 0.9f + mph * 0.1f;

    _s.tripL += lps * dt;                         // trip fuel: everything actually burnt

    // Stationary: leave the average alone (no fuel added, displayed value frozen).
    if (speedKmh < MOVING_KMH) {
      if (nowMs - _lastSaveMs >= SAVE_MS) { save(); _lastSaveMs = nowMs; }
      return;
    }

    _s.curMi += mph * dt / 3600.0f;
    _s.curL  += lps * dt;

    if (_s.curMi >= BUCKET_MI) {                  // commit a distance bucket into the ring
      _s.head = (_s.head + 1) % NB;
      _s.bMi[_s.head] = _s.curMi;
      _s.bL[_s.head]  = _s.curL;
      _s.curMi = 0.0f; _s.curL = 0.0f;
    }
    computeAverages();

    if (nowMs - _lastSaveMs >= SAVE_MS) { save(); _lastSaveMs = nowMs; }  // bounded NVS wear
  }

  float instMpg() const {
    if (_instLps < 1e-6f || _instMph < 1.0f) return 0.0f;
    float mpg = _instMph * IMP_GAL_L / (_instLps * 3600.0f);
    return mpg > 999.0f ? 999.0f : mpg;
  }
  float instL100() const {
    if (_instMph < 1.0f) return 0.0f;
    return (_instLps * 3600.0f) / (_instMph / KMH_TO_MPH) * 100.0f;
  }
  // Rolling 50-mile average, recomputed only while moving (frozen when stationary).
  float avgMpg()  const { return _avgMpg; }
  float avgL100() const { return _avgL100; }
  float tripFuelL() const { return _s.tripL; }

  void save() { _s.magic = MAGIC; _prefs.putBytes(KEY, &_s, sizeof(_s)); }

private:
  static const int      NB            = 50;         // 50 x 1 mi = 50-mile window
  static constexpr float BUCKET_MI     = 1.0f;
  static constexpr float WARMUP_MI     = 1.0f;      // blend inst->avg over the first mile only
  static constexpr float MOVING_KMH    = 1.0f;      // below this the vehicle counts as stationary
  static constexpr float INJ_PER_REV   = 2.5f;      // 5-cyl 4-stroke
  static constexpr float DIESEL_G_PER_L= 832.0f;
  static constexpr float IMP_GAL_L     = 4.54609f;
  static constexpr float KMH_TO_MPH    = 0.621371f;
  static const uint32_t SAVE_MS        = 60000;     // persist at most once/minute
  static const uint32_t MAGIC          = 0x54443502UL;
  static constexpr const char* KEY     = "eco";

  struct State {
    uint32_t magic;
    int      head;
    float    curMi, curL, tripL;
    float    bMi[NB], bL[NB];
  } _s;

  Preferences _prefs;
  uint32_t _lastMs = 0, _lastSaveMs = 0;
  float _instLps = 0, _instMph = 0;
  float _avgMpg = 0, _avgL100 = 0;                 // last moving-average result (frozen at rest)

  // Previous (10-mile, 20 x 0.5 mi) NVS layout - read once to carry history over.
  static const int OLD_NB = 20;
  struct OldState {
    uint32_t magic;
    int      head;
    float    curMi, curL, tripL;
    float    bMi[OLD_NB], bL[OLD_NB];
  };

  void reset() { memset(&_s, 0, sizeof(_s)); _s.magic = MAGIC; _s.head = 0; }

  void migrateOld() {
    OldState o;
    _prefs.getBytes(KEY, &o, sizeof(o));
    reset();
    if (o.magic != MAGIC) return;                  // same magic: the layout only grew
    // Buckets carry their own miles/litres, so the old 0.5-mile buckets sit fine in
    // the new ring; the window is just the sum. Newest first, so ring order holds.
    for (int k = 0; k < OLD_NB; k++) {
      int i = (o.head - k + OLD_NB) % OLD_NB;
      int j = (NB - k) % NB;                       // k=0 -> slot 0 (= head)
      _s.bMi[j] = o.bMi[i];
      _s.bL[j]  = o.bL[i];
    }
    _s.curMi = o.curMi; _s.curL = o.curL; _s.tripL = o.tripL;
    save();
  }

  // Window average, "warmed up" over the first mile: starts at the instantaneous
  // reading and converges to the true window average (w = 0..1). Only called while
  // moving (or at boot), so idling never drags it down.
  void computeAverages() {
    float mi, l; window(mi, l);
    float w = mi / WARMUP_MI; if (w > 1.0f) w = 1.0f;
    float winMpg  = (l < 1e-4f || mi < 1e-3f) ? 0.0f : mi * IMP_GAL_L / l;
    float winL100 = (mi < 1e-3f) ? 0.0f : l / (mi / KMH_TO_MPH) * 100.0f;
    _avgMpg  = instMpg()  * (1.0f - w) + winMpg  * w;
    _avgL100 = instL100() * (1.0f - w) + winL100 * w;
  }
  void window(float& mi, float& l) const {
    mi = _s.curMi; l = _s.curL;
    for (int i = 0; i < NB; i++) { mi += _s.bMi[i]; l += _s.bL[i]; }
  }
};

#endif // TD5_FUEL_ECONOMY_H
