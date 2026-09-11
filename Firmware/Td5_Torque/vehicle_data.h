/*
 * vehicle_data.h - Transport-agnostic snapshot of the vehicle state.
 *
 * This is the single contract shared by every layer:
 *   - DataProvider (sim or Td5) WRITES it
 *   - ObdTranslator READS it to answer ELM327 / OBD-II requests
 *
 * All fields are stored in clean engineering units (SI-ish), NOT raw ECU
 * counts. The Td5Provider is responsible for converting raw ECU values into
 * these units so the OBD layer never has to know about Td5 scaling quirks.
 */

#ifndef TD5_TORQUE_VEHICLE_DATA_H
#define TD5_TORQUE_VEHICLE_DATA_H

#include <Arduino.h>

// Maximum number of active DTCs we will report to the app in one go.
#define MAX_DTCS   32

/*
 * One decoded Diagnostic Trouble Code.
 *
 * The Td5 reports faults as a 35-byte (280-bit) bitfield. Each set bit is a
 * fault whose position maps to the "X-Y" notation used by TD5SPY / Nanocom:
 *     X (category) = byteIndex + 1   (1..35)
 *     Y (sub-code) = bitIndex  + 1   (1..8)
 *     rawIndex     = byteIndex * 8 + bitIndex   (0..279)
 *
 * For the app we emit a standard 2-byte OBD DTC (type + 4 hex digits). Where a
 * Td5 fault corresponds to a real sensor fault we use the genuine P-code (so
 * the app shows its own description); otherwise we synthesise a P1xxx code.
 */
struct DtcEntry {
  uint16_t    rawIndex;   // 0..279 (byteIndex*8 + bitIndex)
  uint8_t     x;          // category 1..35
  uint8_t     y;          // sub-code 1..8
  char        type;       // 'P', 'C', 'B' or 'U'
  uint16_t    code;       // 4 hex digits, e.g. 0x1668 => "P1668"
  const char* desc;       // human-readable text (may be nullptr)
};

struct VehicleData {
  // --- Fuelling / engine -------------------------------------------------
  uint16_t rpm;             // RPM
  uint16_t speedKmh;        // km/h
  float    throttlePct;     // driver demand, %
  float    loadPct;         // calculated engine load, %
  float    injectionMg;     // injection quantity, mg/stroke
  float    torqueLimitMg;   // torque limit, mg/stroke
  float    smokeLimitMg;    // smoke limit, mg/stroke
  float    idleDemandMg;    // idle demand, mg/stroke

  // --- Air / pressures ---------------------------------------------------
  float    mapKpa;          // manifold absolute pressure, kPa
  float    ambientKpa;      // ambient/barometric pressure, kPa
  float    boostBar;        // gauge boost = (map - ambient)/100, bar
  float    mafGs;           // mass air flow, g/s

  // --- Temperatures ------------------------------------------------------
  float    coolantC;        // coolant temp, deg C
  float    intakeAirC;      // inlet air temp, deg C
  float    fuelTempC;       // fuel temp, deg C

  // --- Electrical / actuators -------------------------------------------
  uint16_t batteryMv;       // battery voltage, mV
  float    egrPct;          // EGR modulator position, %
  float    wastegatePct;    // wastegate/turbo modulator position, %
  float    accelTrack1V;    // accelerator track 1, V (PID 0x1B bytes 3-4, BE /1000)
  float    accelTrack2V;    // accelerator track 2, V (PID 0x1B bytes 5-6)
  float    accelTrack3V;    // accelerator track 3, V (PID 0x1B bytes 7-8)
  uint16_t refVoltageMv;    // accelerator 5V supply/reference (PID 0x1B bytes 11-12), mV

  // --- Discrete inputs ---------------------------------------------------
  uint8_t  gear;            // selected gear 0..6 (0 = none/unknown)
  bool     brakePressed;    // 0x1E DB2 bit7 (active-low)
  bool     clutchPressed;   // 0x1E DB1 bit1 (active-low)
  bool     cruiseMaster;    // 0x1E DB1 bit2 - cruise master switch on
  bool     cruiseSet;       // 0x1E DB1 bit3 - cruise set/accelerate
  bool     cruiseResume;    // 0x1E DB1 bit4 - cruise resume
  bool     acRequest;       // 0x1E DB2 bit3 - A/C compressor request
  bool     transferHigh;    // 0x1E DB2 bit6 - transfer box HIGH ratio (lit when not low)

  // --- Injector fuelling (relative trim per cylinder) --------------------
  int16_t  injTrim[5];      // PID 0x40, signed - per-cylinder fuelling balance

  // --- Status ------------------------------------------------------------
  bool     ecuConnected;    // true once the data source is live
  uint32_t runtimeSec;      // seconds the engine has been "running"

  // --- Fault codes -------------------------------------------------------
  uint8_t  dtcCount;
  DtcEntry dtcs[MAX_DTCS];

  // Convenience
  float batteryVolts() const { return batteryMv / 1000.0f; }
};

#endif // TD5_TORQUE_VEHICLE_DATA_H
