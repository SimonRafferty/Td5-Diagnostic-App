/*
 * fuel_economy.h - rolling fuel-economy estimator for the Td5 diagnostic dongle.
 *
 * Integrates fuel volume (from PID 0x1D injection quantity + PID 0x09 RPM) against
 * distance (from PID 0x0D road speed) to produce instantaneous and last-10-mile
 * average economy. The distance-bucketed window and a lifetime trip-fuel total are
 * persisted to NVS (Preferences) so the average survives ignition/power cycles.
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
    if (_prefs.getBytesLength(KEY) == sizeof(_s)) {
      _prefs.getBytes(KEY, &_s, sizeof(_s));
      if (_s.magic != MAGIC) reset();
    } else {
      reset();
    }
    _lastMs = 0; _lastSaveMs = 0; _instLps = 0; _instMph = 0;
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

    _s.curMi += mph * dt / 3600.0f;
    _s.curL  += lps * dt;
    _s.tripL += lps * dt;

    if (_s.curMi >= BUCKET_MI) {                  // commit a distance bucket into the ring
      _s.head = (_s.head + 1) % NB;
      _s.bMi[_s.head] = _s.curMi;
      _s.bL[_s.head]  = _s.curL;
      _s.curMi = 0.0f; _s.curL = 0.0f;
    }

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
  // Rolling 10-mile average, "warmed up": starts at the instantaneous reading and
  // converges to the true window average as the 10-mile window fills (w = 0..1).
  float avgMpg() const {
    float mi, l; window(mi, l);
    float win = (l < 1e-4f || mi < 1e-3f) ? 0.0f : mi * IMP_GAL_L / l;
    float w = mi / WINDOW_MI; if (w > 1.0f) w = 1.0f;
    return instMpg() * (1.0f - w) + win * w;
  }
  float avgL100() const {
    float mi, l; window(mi, l);
    float win = (mi < 1e-3f) ? 0.0f : l / (mi / KMH_TO_MPH) * 100.0f;
    float w = mi / WINDOW_MI; if (w > 1.0f) w = 1.0f;
    return instL100() * (1.0f - w) + win * w;
  }
  float tripFuelL() const { return _s.tripL; }

  void save() { _s.magic = MAGIC; _prefs.putBytes(KEY, &_s, sizeof(_s)); }

private:
  static const int      NB            = 20;         // 20 x 0.5 mi = 10-mile window
  static constexpr float BUCKET_MI     = 0.5f;
  static constexpr float WINDOW_MI     = NB * BUCKET_MI;  // full window distance (miles)
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

  void reset() { memset(&_s, 0, sizeof(_s)); _s.magic = MAGIC; _s.head = 0; }
  void window(float& mi, float& l) const {
    mi = _s.curMi; l = _s.curL;
    for (int i = 0; i < NB; i++) { mi += _s.bMi[i]; l += _s.bL[i]; }
  }
};

#endif // TD5_FUEL_ECONOMY_H
