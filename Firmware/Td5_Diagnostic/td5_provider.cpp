/*
 * td5_provider.cpp - real Td5 ECU data source. See td5_provider.h.
 *
 * Scaling note: the Td5Comm library's own getfValue() has legacy/unreliable
 * scaling for the composite PIDs, so we extract bytes directly from each PID's
 * response frame (bytes 0=len, 1=0x61, 2=sub-PID, 3..=data). Formulas marked
 * "VERIFY" should be checked against a known-good tool on the vehicle; they are
 * the best cut from the reverse-engineering notes and are easy to tweak here.
 */

#include "td5_provider.h"
#if !DATA_SOURCE_SIM

#include <math.h>
#include "td5_dtc_table.h"

// --- response-frame byte helpers ------------------------------------------
static inline uint16_t be(Td5Pid& p, uint8_t i) {
  return ((uint16_t)p.getResponseByte(i) << 8) | p.getResponseByte(i + 1);
}
static inline uint16_t le(Td5Pid& p, uint8_t i) {   // little-endian (PID 0x23 only)
  return ((uint16_t)p.getResponseByte(i + 1) << 8) | p.getResponseByte(i);
}
static inline uint8_t bcd8(uint8_t b) { return (uint8_t)((b >> 4) * 10 + (b & 0x0F)); }

// MAF raw -> g/s. MEMSTools reports kg/h; the raw appears to be kg/h x100, so
// g/s = raw / 100 / 3.6 = raw / 360. VERIFY/calibrate on a running engine.
#define TD5_MAF_RAW_TO_GS (1.0f / 360.0f)

// Number of PIDs in the round-robin.
#define TD5_POLL_COUNT 15

bool Td5Provider::begin() {
  _d = VehicleData{};
  _d.ecuConnected = false;
  _d.ambientKpa   = 101.0f;   // benign defaults until the ECU is polled
  _d.coolantC     = 20.0f;
  _d.batteryMv    = 12600;
  _fuelEco.begin();           // load the persisted 10-mile economy window from NVS
  _td5.init();
  _lastAttempt = 0;
  // Run all K-line I/O in a dedicated task pinned to core 0, so the blocking
  // connect/getPid never stall the Arduino loop (core 1) that services BLE.
  xTaskCreatePinnedToCore(&Td5Provider::taskEntry, "td5kline", 8192, this, 1, &_task, 0);
  return true;
}

// poll() is called from the main loop() but does nothing here - the K-line is
// driven by the dedicated task below. The OBD/app path only ever reads _d.
void Td5Provider::poll() {}

void Td5Provider::taskEntry(void* arg) {
  static_cast<Td5Provider*>(arg)->runLoop();
}

void Td5Provider::runLoop() {
  for (;;) {
    pollStep();
    vTaskDelay(1);   // yield ~1 tick so the idle task / watchdog is fed
  }
}

void Td5Provider::pollStep() {
  // --- Not connected: probe for the ECU (connectToEcu() is blocking) -------
  if (!_td5.ecuIsConnected()) {
    unsigned long now = millis();
    unsigned long probeGap = _demoActive ? 15000 : 5000;  // probe less in demo (keeps it smooth)
    if (_lastAttempt == 0 || now - _lastAttempt >= probeGap) {
      _lastAttempt = now;
#if DEBUG_SERIAL
      Serial.println("[TD5] connecting to ECU...");
#endif
      if (_td5.connectToEcu(false)) {
        _realEver       = true;
        _demoActive     = false;   // real ECU wins; leave demo for good
        _d.ecuConnected = true;
        _connectMs      = millis();
        _lastGood       = millis();
        _lastKeepAlive  = millis();
        _pollIdx        = 0;
        _lastDtcPoll    = 0;      // read fault codes into the buffer soon
        _clearPending   = false;
        readEcuIdentity();        // one-shot VIN + map/fuel/homologation (static)
#if DEBUG_SERIAL
        Serial.println("[TD5] ECU connected");
#endif
        return;
      }
    }
    // No real ECU connected -> report disconnected. Demo/synthetic data removed:
    // the pipeline is proven and showing values with the engine off is misleading.
    _d.ecuConnected = false;
    return;
  }

  // --- Connected -----------------------------------------------------------
  unsigned long now = millis();

  // Keep-alive (tester present) every 1.5 s or the ECU drops us. Only advance
  // the timer when the frame was actually sent - getPid() returns PID_NOT_READY
  // without transmitting if it lands inside the 55 ms inter-request gate, so we
  // retry next loop rather than deferring a full 1.5 s.
  if (now - _lastKeepAlive >= 1500) {
    if (_td5.getPid(&pidKeepAlive) != PID_NOT_READY) _lastKeepAlive = now;
  }

  // ONE K-line data transaction per loop (the 55 ms gate paces the wire). The
  // ESP owns ALL K-line timing here; the OBD/app side only ever reads the
  // buffer, so displaying 1 or 20 PIDs makes no difference to the bus. Priority:
  // a pending Clear-Codes, then a periodic DTC refresh, else the next live PID.
  if (_clearPending) {
    if (doClear()) { _clearPending = false; _lastDtcPoll = 0; }  // then re-read DTCs
  } else if (now - _lastDtcPoll >= 4000) {
    if (refreshDtcBuffer()) _lastDtcPoll = now;   // advance only on a valid frame
  } else {
    pollNext();
  }

  _d.runtimeSec   = (millis() - _connectMs) / 1000UL;
  _d.ecuConnected = true;

  // Fuel economy: integrate fuel (injection x rpm) vs distance (speed) each loop.
  _fuelEco.update(_d.speedKmh, _d.rpm, _d.injectionMg, now);
  _d.instMpg   = _fuelEco.instMpg();
  _d.avgMpg    = _fuelEco.avgMpg();
  _d.avgL100   = _fuelEco.avgL100();
  _d.tripFuelL = _fuelEco.tripFuelL();

#if TD5_DEBUG_FRAMES
  // Periodic dump of the DECODED buffer, so a serial capture shows both the raw
  // frames (from pollNext/refreshDtcBuffer) and the values they produced.
  static unsigned long _lastDump = 0;
  if (millis() - _lastDump >= 1000) {
    _lastDump = millis();
    Serial.printf("[VAL] rpm=%u spd=%u load=%.0f cool=%.0f intk=%.0f fuel=%.0f "
                  "map=%.0f maf=%.1f amb=%.0f batt=%u tps=%.0f egr=%.0f wg=%.0f dtc=%u\n",
                  _d.rpm, _d.speedKmh, _d.loadPct, _d.coolantC, _d.intakeAirC, _d.fuelTempC,
                  _d.mapKpa, _d.mafGs, _d.ambientKpa, _d.batteryMv, _d.throttlePct,
                  _d.egrPct, _d.wastegatePct, _d.dtcCount);
  }
#endif

  // No good K-line data for 5 s -> assume the link dropped; force a reconnect.
  if (millis() - _lastGood >= 5000) {
#if DEBUG_SERIAL
    Serial.println("[TD5] data timeout; disconnecting");
#endif
    _td5.disconnectFromEcu();
    _d.ecuConnected = false;
    _d.dtcCount = 0;       // don't report a stale fault count / MIL while down
  }
}

void Td5Provider::pollNext() {
  // Select this slot's PID and the minimum response length its decode needs
  // (highest byte offset + 1). Frame layout: [0]=len [1]=0x61 [2]=sub [3..]=data.
  Td5Pid* pid;
  uint8_t minLen;
  switch (_pollIdx) {
    case 0:  pid = &pidRPM;              minLen = 5;  break;  // 0x09 RPM        bytes 3-4
    case 1:  pid = &pidVehicleSpeed;     minLen = 4;  break;  // 0x0D speed      byte 3
    case 2:  pid = &pidTemperatures;     minLen = 19; break;  // 0x1A temps+sensorV bytes 3-18
    case 3:  pid = &pidThrottlePosition;    minLen = 9;  break;  // 0x1B accel tracks 1/2/3 + 5V supply
    case 4:  pid = &pidTurboPressureMaf; minLen = 11; break;  // 0x1C MAP+MAF   bytes 3-10
    case 5:  pid = &pidBatteryVoltage;   minLen = 7;  break;  // 0x10 battery + direct bytes 3-6
    case 6:  pid = &pidCruiseBrakeSwitches; minLen = 5;  break;  // 0x1E brake/clutch/cruise/ignition switches
    case 7:  pid = &pidEGR;              minLen = 5;  break;  // 0x37 EGR        bytes 3-4
    case 8:  pid = &pidILT;              minLen = 5;  break;  // 0x38 wastegate  bytes 3-4
    case 9:  pid = &pidFuelling;         minLen = 19; break;  // 0x1D fuelling   bytes 3-18
    case 10: pid = &pidAmbientPressure;     minLen = 7;  break;  // 0x23 ambient + direct bytes 3-6
    case 11: pid = &pidInjectorsBalance; minLen = 13; break;  // 0x40 roughness bytes 3-12
    case 12: pid = &pidRelayOutputs;     minLen = 5;  break;  // 0x36 relay/output bits 3-4
    case 13: pid = &pidEgrInlet;         minLen = 5;  break;  // 0x45 EGR inlet throttle 3-4
    case 14: pid = &pidDigitalInputs;    minLen = 5;  break;  // 0x21 idle speed error (signed) 3-4
    default: _pollIdx = 0; return;
  }

  int8_t r = _td5.getPid(pid);
  if (r == PID_NOT_READY) return;   // 55 ms inter-request gate still closed:
                                    // retry THIS slot next loop, don't advance.

  // Accept only a positive (0x61) response whose echoed sub-PID matches the one
  // we requested and that is long enough to decode. This rejects a desynced /
  // lagged frame (another PID's reply) rather than decoding another PID's bytes
  // as this parameter.
  bool valid = (r >= (int8_t)minLen)
            && (pid->getResponseByte(1) == 0x61)
            && (pid->getResponseByte(2) == pid->requestFrame[2]);

#if TD5_DEBUG_FRAMES
  if (r > 0) {
    Serial.printf("[TD5] req %02X len %d%s:", pid->requestFrame[2], r, valid ? "" : " REJECT");
    for (int8_t bi = 0; bi < r && bi < 24; bi++) Serial.printf(" %02X", pid->getResponseByte(bi));
    Serial.println();
  }
#endif

  if (valid) {
    switch (_pollIdx) {
      case 0:  _d.rpm = be(pidRPM, 3); break;
      case 1:  _d.speedKmh = pidVehicleSpeed.getResponseByte(3); break;
      case 2:  // PID 0x1A = TEMPERATURE composite (all Kelvin x10) + sensor voltages
        _d.coolantC         = ((int16_t)be(pidTemperatures, 3)  - 2732) / 10.0f;  // off0
        _d.coolantSensorV   =  be(pidTemperatures, 5)  / 1000.0f;                 // off2
        _d.intakeAirC       = ((int16_t)be(pidTemperatures, 7)  - 2732) / 10.0f;  // off4
        _d.intakeAirSensorV =  be(pidTemperatures, 9)  / 1000.0f;                 // off6
        _d.fuelTempC        = ((int16_t)be(pidTemperatures, 15) - 2732) / 10.0f;  // off0C
        _d.fuelTempSensorV  =  be(pidTemperatures, 17) / 1000.0f;                 // off0E
        break;
      case 3:  // PID 0x1B = accelerator tracks (BE, /1000 V); track1+track2 ~= supply
        _d.accelTrack1V = be(pidThrottlePosition, 3) / 1000.0f;
        _d.accelTrack2V = be(pidThrottlePosition, 5) / 1000.0f;
        _d.accelTrack3V = be(pidThrottlePosition, 7) / 1000.0f;
        if (r >= 13) _d.refVoltageMv = be(pidThrottlePosition, 11);    // 5V sensor supply (bytes 11-12)
        break;
      case 4:  // PID 0x1C = PRESSURE / AIRFLOW composite (was mislabelled temps)
        _d.mapKpa       = be(pidTurboPressureMaf, 3) / 100.0f;                 // off0 MAP
        _d.mapDirectKpa = be(pidTurboPressureMaf, 5) / 100.0f;                 // off2 MAP direct
        _d.mafGs        = be(pidTurboPressureMaf, 7) * TD5_MAF_RAW_TO_GS;      // off4 MAF (VERIFY scale)
        _d.mafSensorV   = be(pidTurboPressureMaf, 9) / 1000.0f;               // off6 MAF sensor V
        _d.boostBar     = (_d.mapKpa - _d.ambientKpa) / 100.0f;
        break;
      case 5:  // 0x10 battery voltage + direct reading (5V supply now from 0x1B)
        _d.batteryMv       = be(pidBatteryVoltage, 3);   // off0
        _d.batteryDirectMv = be(pidBatteryVoltage, 5);   // off2
        break;
      case 6: {  // PID 0x1E switches - EA2EGA-validated bit map. DB1=byte3, DB2=byte4.
        uint8_t b1 = pidCruiseBrakeSwitches.getResponseByte(3);  // DB1
        uint8_t b2 = pidCruiseBrakeSwitches.getResponseByte(4);  // DB2
        _d.brakePressed  = (b2 & 0x80) == 0;   // DB2 bit7, active-low (CONFIRMED)
        _d.clutchPressed = (b1 & 0x02) == 0;   // DB1 bit1, active-low (CONFIRMED)
        _d.cruiseMaster  = (b1 & 0x04) != 0;   // DB1 bit2 cruise master
        _d.cruiseSet     = (b1 & 0x08) != 0;   // DB1 bit3 cruise set/accel
        _d.cruiseResume  = (b1 & 0x10) != 0;   // DB1 bit4 cruise resume
        _d.acRequest       = (b2 & 0x08) != 0;   // DB2 bit3 A/C switch (panel input)
        _d.transferHigh    = (b2 & 0x40) == 0;   // DB2 bit6 set = low ratio -> High when clear
        _d.ignitionOn      = (b2 & 0x02) != 0;   // DB2 bit1 ignition switch
        _d.securityLinkHigh= (b2 & 0x20) != 0;   // DB2 bit5 security link
        _d.gearboxNeutral  = (b2 & 0x08) != 0;   // DB2 bit3 auto-box neutral (shares A/C-switch input; VERIFY on OEM auto)
        break;
      }
      case 7:  _d.egrPct       = be(pidEGR, 3) / 100.0f; break;        // VERIFY
      case 8:  _d.wastegatePct = be(pidILT, 3) / 100.0f; break;        // VERIFY
      case 9: {
        int16_t dd = (int16_t)be(pidFuelling, 3);          // driver demand is SIGNED
        _d.throttlePct   = (dd < 0) ? 0.0f : dd / 100.0f;  // small -ve zero-offset -> 0
        _d.injectionMg   = be(pidFuelling, 9)  / 100.0f;   // off6 injected quantity
        _d.smokeLimitMg  = be(pidFuelling, 13) / 100.0f;   // off0A smoke map limit
        _d.torqueLimitMg = be(pidFuelling, 15) / 100.0f;   // off0C torque map limit
        _d.idleDemandMg  = be(pidFuelling, 17) / 100.0f;   // off0E idle demand
        _d.loadPct       = _d.throttlePct;                 // proxy for engine load
        break;
      }
      case 10:  // PID 0x23 = ambient/barometric pressure + direct reading (BE, /100 kPa)
        _d.ambientKpa       = be(pidAmbientPressure, 3) / 100.0f;   // off0
        _d.ambientDirectKpa = be(pidAmbientPressure, 5) / 100.0f;   // off2
        _d.boostBar         = (_d.mapKpa - _d.ambientKpa) / 100.0f;
        break;
      case 11:  // PID 0x40 = per-cylinder roughness (5 x signed int16, BE, RPM)
        for (uint8_t c = 0; c < 5; c++)
          _d.injTrim[c] = (int16_t)be(pidInjectorsBalance, 3 + c * 2);
        break;
      case 12: {  // PID 0x36 = relay / output status bitfield
        uint8_t r0 = pidRelayOutputs.getResponseByte(3);   // off0
        uint8_t r1 = pidRelayOutputs.getResponseByte(4);   // off1
        _d.radFanDrive   = (r0 & 0x02) != 0;  // off0 bit1
        _d.mainRelay     = (r1 & 0x01) != 0;  // off1 bit0
        _d.fuelPumpRelay = (r1 & 0x04) != 0;  // off1 bit2
        _d.acClutchDrive = (r1 & 0x08) != 0;  // off1 bit3
        _d.milOn         = (r1 & 0x10) == 0;  // off1 bit4 - MIL drive is ACTIVE-LOW (1 = lamp off)
        _d.glowPlugLight = (r1 & 0x20) != 0;  // off1 bit5
        _d.glowPlugRelay = (r1 & 0x40) != 0;  // off1 bit6
        break;
      }
      case 13:  // PID 0x45 = EGR inlet throttle
        _d.egrInletPct = be(pidEgrInlet, 3) / 100.0f;
        break;
      case 14:  // PID 0x21 = idle speed error (signed RPM)
        _d.idleSpeedErrorRpm = (int16_t)be(pidDigitalInputs, 3);
        break;
    }
    _lastGood = millis();
  }

  // A frame was transmitted (success, lost, or negative) -> advance the rotation.
  _pollIdx = (_pollIdx + 1) % TD5_POLL_COUNT;
}

// One-shot ECU identity read after a fresh connect. VIN needs a flash ECU
// (service 1A 87); map/fuel/homologation come from service 21 32. Both are
// static, so we read once and cache. Retries clear the 55 ms inter-request
// gate; failures (e.g. MSB ECU with no VIN) simply leave the strings empty.
void Td5Provider::readEcuIdentity() {
  _d.vin[0] = _d.mapName[0] = _d.fuelVariant[0] = _d.homologation[0] = '\0';

  // VIN: 02 1A 87 -> [len] 5A 87 <11 ASCII><3 BCD>...
  for (uint8_t attempt = 0; attempt < 25; attempt++) {
    int8_t r = _td5.getPid(&pidVin);
    if (r == PID_NOT_READY) { delay(10); continue; }
    if (r >= 17 && pidVin.getResponseByte(1) != 0x7F) {
      for (uint8_t i = 0; i < 11; i++) _d.vin[i] = (char)pidVin.getResponseByte(3 + i);
      snprintf(_d.vin + 11, 7, "%02u%02u%02u",
               bcd8(pidVin.getResponseByte(14)),
               bcd8(pidVin.getResponseByte(15)),
               bcd8(pidVin.getResponseByte(16)));
      _d.vin[17] = '\0';
    }
    break;   // transmitted (or negative) -> stop retrying
  }

  // Map / fuel / homologation: 02 21 32 -> [len] 61 32 <8 map><8 fuel><4 homolog>...
  for (uint8_t attempt = 0; attempt < 25; attempt++) {
    int8_t r = _td5.getPid(&pidMapName);
    if (r == PID_NOT_READY) { delay(10); continue; }
    if (r >= 23 && pidMapName.getResponseByte(1) != 0x7F
                && pidMapName.getResponseByte(2) == 0x32) {
      for (uint8_t i = 0; i < 8; i++) _d.mapName[i]      = (char)pidMapName.getResponseByte(3 + i);
      for (uint8_t i = 0; i < 8; i++) _d.fuelVariant[i]  = (char)pidMapName.getResponseByte(11 + i);
      for (uint8_t i = 0; i < 4; i++) _d.homologation[i] = (char)pidMapName.getResponseByte(19 + i);
      _d.mapName[8] = _d.fuelVariant[8] = '\0';
      _d.homologation[4] = '\0';
    }
    break;
  }

  // Accelerator pedal type from feature flags (PID 0x20 offset0 bit7): 3-way vs 2-way.
  // Reuses pidStartFuelling, whose request is 02 21 20 (0x20 = feature flags, NOT injection).
  for (uint8_t attempt = 0; attempt < 25; attempt++) {
    int8_t r = _td5.getPid(&pidStartFuelling);
    if (r == PID_NOT_READY) { delay(10); continue; }
    if (r >= 4 && pidStartFuelling.getResponseByte(1) == 0x61
               && pidStartFuelling.getResponseByte(2) == 0x20) {
      _d.pedalTracks = (pidStartFuelling.getResponseByte(3) & 0x80) ? 3 : 2;   // off0 bit7
    }
    break;
  }

#if DEBUG_SERIAL
  Serial.printf("[TD5] VIN='%s' map='%s' fuel='%s' homolog='%s' pedal=%u-track\n",
                _d.vin, _d.mapName, _d.fuelVariant, _d.homologation, _d.pedalTracks);
#endif
}

// --- DTCs ------------------------------------------------------------------

void Td5Provider::buildDtc(int rawIndex, DtcEntry& e) {
  e.rawIndex = (uint16_t)rawIndex;
  e.x = (uint8_t)(rawIndex / 8) + 1;   // 1-based category
  e.y = (uint8_t)(rawIndex % 8) + 1;   // 1-based sub-code

  const Td5DtcDef* def = lookupTd5Dtc(e.x, e.y);
  e.desc = def ? def->desc : nullptr;

  if (def && def->pcode[0] != '\0') {
    // Genuine (inferred) OBD code, e.g. "P0115" / "U0001".
    e.type = def->pcode[0];
    e.code = (uint16_t)strtoul(def->pcode + 1, nullptr, 16);
  } else {
    // No OBD equivalent: synthesise a unique P1xxx from the bit index so the
    // code is still stable and identifiable (P1000 + rawIndex).
    e.type = 'P';
    e.code = (uint16_t)(0x1000 + rawIndex);
  }
}

// --- Demo data (bench, no ECU) ----------------------------------------------
// Plausible, smoothly-varying synthetic values + two demo DTCs, so the app/BLE
// path (and the OBD framing) can be exercised without a vehicle.
void Td5Provider::fillDemo() {
  float t   = millis() / 1000.0f;
  float rev = 0.5f + 0.5f * sinf(t * 0.30f);              // 0..1

  _d.rpm         = (uint16_t)(800.0f + rev * 1700.0f);
  _d.throttlePct = 5.0f + rev * 70.0f;
  _d.loadPct     = _d.throttlePct;
  _d.speedKmh    = (uint16_t)((0.5f + 0.5f * sinf(t * 0.15f + 1.0f)) * 110.0f);

  float warm     = 20.0f + t * 0.8f; if (warm > 88.0f) warm = 88.0f;
  _d.coolantC    = warm + sinf(t * 0.05f);
  _d.intakeAirC  = 25.0f + sinf(t * 0.10f) * 6.0f;
  _d.fuelTempC   = 40.0f;

  _d.ambientKpa  = 101.0f;
  _d.mapKpa      = 100.0f + (_d.throttlePct / 100.0f) * 130.0f;
  _d.boostBar    = (_d.mapKpa - _d.ambientKpa) / 100.0f;
  _d.mafGs       = 5.0f + rev * 45.0f;
  _d.batteryMv   = (uint16_t)(14200.0f + sinf(t * 0.20f) * 80.0f);
  _d.egrPct      = 20.0f;
  _d.wastegatePct= (_d.boostBar / 1.3f) * 100.0f; if (_d.wastegatePct < 0) _d.wastegatePct = 0;
  _d.injectionMg = 6.0f + rev * 40.0f;
  _d.accelTrack1V= 0.5f + (_d.throttlePct / 100.0f) * 4.0f;
  _d.accelTrack2V= 5.0f - _d.accelTrack1V;
  _d.accelTrack3V= _d.accelTrack2V;
  for (int c = 0; c < 5; c++)                              // small swinging balance
    _d.injTrim[c] = (int16_t)lroundf(sinf(t * 0.4f + c) * 4.0f);
  _d.gear        = _d.speedKmh < 3 ? 0 : (_d.speedKmh < 40 ? 2 : 4);
  _d.runtimeSec  = (uint32_t)t;
  _d.ecuConnected = true;        // present as "connected" so the app polls & displays

  if (_demoFaultsCleared) {
    _d.dtcCount = 0;
  } else {
    _d.dtcs[0] = { 42,  6, 3, 'P', 0x0115, "Coolant temp sensor circuit" };
    _d.dtcs[1] = { 90, 12, 3, 'P', 0x1668, "Injector cylinder circuit (Td5)" };
    _d.dtcCount = 2;
  }

#if TD5_DEBUG_FRAMES
  static unsigned long _lastDemoDump = 0;
  if (millis() - _lastDemoDump >= 1000) {
    _lastDemoDump = millis();
    Serial.printf("[DEMO] rpm=%u spd=%u cool=%.0f batt=%u tps=%.0f map=%.0f maf=%.1f dtc=%u\n",
                  _d.rpm, _d.speedKmh, _d.coolantC, _d.batteryMv, _d.throttlePct,
                  _d.mapKpa, _d.mafGs, _d.dtcCount);
  }
#endif
}

// --- OBD-path entry points: NON-BLOCKING, buffer only -----------------------
// These are called from the app/ELM path. They must NOT touch the K-line, so
// the app's poll rate can never interfere with the ECU polling. The buffer is
// maintained by poll() on the ESP's own schedule.

int Td5Provider::readDTCs() {
  return _d.dtcCount;          // last value refreshed by poll()/refreshDtcBuffer()
}

bool Td5Provider::clearDTCs() {
  // Accept the clear for a live ECU OR the bench/demo source; reject only when
  // genuinely disconnected. poll() applies it on its next slot: in demo it sets
  // _demoFaultsCleared (dtcCount -> 0); on the vehicle it sends 31 DD. Returning
  // true here makes mode 04 answer "44" (accepted), as apps expect.
  if (!_demoActive && !_td5.ecuIsConnected()) return false;
  _clearPending = true;        // poll() transmits/applies the clear on its next slot
  return true;
}

// --- K-line-side helpers: called ONLY from poll() (single-task, paced) -------

bool Td5Provider::refreshDtcBuffer() {
  int n = _td5.getFaultCodes();          // requests 02 21 3B, reads bitfield, counts bits
  // Only trust the count if the frame is genuinely the fault-code reply
  // (0x61 = positive response to 0x21, sub-PID 0x3B). A gated/desynced frame
  // must NOT be reported as "0 faults".
  if (n < 0 ||
      pidFaultCodes.getResponseByte(1) != 0x61 ||
      pidFaultCodes.getResponseByte(2) != 0x3B) {
    return false;                        // gate/lost/mismatch -> retry next loop
  }
  _lastGood = millis();

#if TD5_DEBUG_FRAMES
  Serial.printf("[TD5] DTC frame, %d codes:", n);
  for (int bi = 0; bi < 39; bi++) Serial.printf(" %02X", pidFaultCodes.getResponseByte(bi));
  Serial.println();
#endif

  uint8_t cnt = 0;
  for (int i = 0; i < n && cnt < MAX_DTCS; i++) {
    int raw = _td5.getFaultCode(i);
    if (raw < 0) break;
    buildDtc(raw, _d.dtcs[cnt]);
    if (_d.dtcs[cnt].desc == nullptr) continue;   // skip undefined bits (Nanocom hides them too)
    cnt++;
  }
  _d.dtcCount = cnt;
  return true;
}

bool Td5Provider::doClear() {
  int8_t r = _td5.resetFaults();
  if (r == PID_NOT_READY) return false;  // 55 ms gate: not sent, retry next loop
  if (r > 0) _d.dtcCount = 0;            // positive response -> codes cleared
  _lastGood = millis();
  return true;                           // frame transmitted -> stop retrying
}

#endif // !DATA_SOURCE_SIM
