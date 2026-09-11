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

// Number of PIDs in the round-robin.
#define TD5_POLL_COUNT 12

bool Td5Provider::begin() {
  _d = VehicleData{};
  _d.ecuConnected = false;
  _d.ambientKpa   = 101.0f;   // benign defaults until the ECU is polled
  _d.coolantC     = 20.0f;
  _d.batteryMv    = 12600;
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
    case 2:  pid = &pidTemperatures;     minLen = 7;  break;  // 0x1A cool+MAP   bytes 3-6
    case 3:  pid = &pidThrottlePosition;    minLen = 9;  break;  // 0x1B accel tracks 1/2/3 + 5V supply
    case 4:  pid = &pidTurboPressureMaf; minLen = 9;  break;  // 0x1C iat(4)+fuel bytes 7-8
    case 5:  pid = &pidBatteryVoltage;   minLen = 5;  break;  // 0x10 battery    bytes 3-4
    case 6:  pid = &pidCruiseBrakeSwitches; minLen = 5;  break;  // 0x1E brake/handbrake/cruise switches
    case 7:  pid = &pidEGR;              minLen = 5;  break;  // 0x37 EGR        bytes 3-4
    case 8:  pid = &pidILT;              minLen = 5;  break;  // 0x38 wastegate  bytes 3-4
    case 9:  pid = &pidFuelling;         minLen = 19; break;  // 0x1D fuelling   bytes 3-18
    case 10: pid = &pidAmbientPressure;     minLen = 5;  break;  // 0x23 ambient pressure (BE)
    case 11: pid = &pidInjectorsBalance; minLen = 13; break;  // 0x40 injector trims bytes 3-12
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
      case 2:
        _d.coolantC = ((int16_t)be(pidTemperatures, 3) - 2732) / 10.0f;
        _d.mapKpa   = be(pidTemperatures, 5) / 100.0f;                 // VERIFY scaling
        _d.boostBar = (_d.mapKpa - _d.ambientKpa) / 100.0f;
        break;
      case 3:  // PID 0x1B = accelerator tracks (BE, /1000 V); track1+track2 ~= supply
        _d.accelTrack1V = be(pidThrottlePosition, 3) / 1000.0f;
        _d.accelTrack2V = be(pidThrottlePosition, 5) / 1000.0f;
        _d.accelTrack3V = be(pidThrottlePosition, 7) / 1000.0f;
        if (r >= 13) _d.refVoltageMv = be(pidThrottlePosition, 11);    // 5V sensor supply (bytes 11-12)
        break;
      case 4:
        // Matches the original app: inlet air = byte 4 / 10; fuel temp = (bytes 7-8) - 20.
        _d.intakeAirC = pidTurboPressureMaf.getResponseByte(4) / 10.0f;        // VERIFY
        _d.fuelTempC  = (float)((int16_t)be(pidTurboPressureMaf, 7) - 20);     // VERIFY
        break;
      case 5:  _d.batteryMv = be(pidBatteryVoltage, 3); break;   // 0x10 battery (5V supply now from 0x1B)
      case 6: {  // PID 0x1E switches - EA2EGA-validated bit map. DB1=byte3, DB2=byte4.
        uint8_t b1 = pidCruiseBrakeSwitches.getResponseByte(3);  // DB1
        uint8_t b2 = pidCruiseBrakeSwitches.getResponseByte(4);  // DB2
        _d.brakePressed  = (b2 & 0x80) == 0;   // DB2 bit7, active-low (CONFIRMED)
        _d.clutchPressed = (b1 & 0x02) == 0;   // DB1 bit1, active-low (CONFIRMED)
        _d.cruiseMaster  = (b1 & 0x04) != 0;   // DB1 bit2 cruise master
        _d.cruiseSet     = (b1 & 0x08) != 0;   // DB1 bit3 cruise set/accel
        _d.cruiseResume  = (b1 & 0x10) != 0;   // DB1 bit4 cruise resume
        _d.acRequest     = (b2 & 0x08) != 0;   // DB2 bit3 A/C clutch request
        _d.transferHigh  = (b2 & 0x40) == 0;   // DB2 bit6 set = low ratio -> High when clear
        break;
      }
      case 7:  _d.egrPct       = be(pidEGR, 3) / 100.0f; break;        // VERIFY
      case 8:  _d.wastegatePct = be(pidILT, 3) / 100.0f; break;        // VERIFY
      case 9: {
        int16_t dd = (int16_t)be(pidFuelling, 3);          // driver demand is SIGNED
        _d.throttlePct   = (dd < 0) ? 0.0f : dd / 100.0f;  // small -ve zero-offset -> 0
        _d.injectionMg   = be(pidFuelling, 9)  / 100.0f;   // injection quantity
        _d.torqueLimitMg = be(pidFuelling, 13) / 100.0f;
        _d.smokeLimitMg  = be(pidFuelling, 15) / 100.0f;
        _d.idleDemandMg  = be(pidFuelling, 17) / 100.0f;
        _d.loadPct       = _d.throttlePct;                 // proxy for engine load
        break;
      }
      case 10:  // PID 0x23 = ambient/barometric pressure (BE, /100 kPa)
        _d.ambientKpa = be(pidAmbientPressure, 3) / 100.0f;
        _d.boostBar   = (_d.mapKpa - _d.ambientKpa) / 100.0f;
        break;
      case 11:  // PID 0x40 = per-cylinder fuelling trim (5 x signed int16, BE)
        for (uint8_t c = 0; c < 5; c++)
          _d.injTrim[c] = (int16_t)be(pidInjectorsBalance, 3 + c * 2);
        break;
    }
    _lastGood = millis();
  }

  // A frame was transmitted (success, lost, or negative) -> advance the rotation.
  _pollIdx = (_pollIdx + 1) % TD5_POLL_COUNT;
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
