/*
 * obd_pids.cpp - OBD-II translation layer implementation.
 * See obd_pids.h for the overview.
 */

#include "obd_pids.h"

const char* OBD_NO_DATA = "NO DATA";

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static String hx(uint8_t b) {
  char t[3];
  snprintf(t, sizeof(t), "%02X", b);
  return String(t);
}

static uint8_t clamp8(long v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return (uint8_t)v;
}

static uint16_t clamp16(long v) {
  if (v < 0) return 0;
  if (v > 65535) return 65535;
  return (uint16_t)v;
}

// Append a 16-bit value as two space-separated hex bytes: " HH LL"
static void appendU16(String& s, uint16_t v) {
  s += ' ';
  s += hx((uint8_t)(v >> 8));
  s += ' ';
  s += hx((uint8_t)(v & 0xFF));
}

// Append an ASCII string as space-separated hex bytes (up to maxLen or NUL).
static void appendAscii(String& s, const char* p, uint8_t maxLen) {
  for (uint8_t i = 0; i < maxLen && p[i]; i++) { s += ' '; s += hx((uint8_t)p[i]); }
}

// The mode-01 PIDs we implement. 0x00/0x20/0x40 are the range-continuation
// indicators (they return the support bitmasks and also flag the next range).
static const uint8_t SUPPORTED_01[] = {
  0x00, 0x01, 0x04, 0x05, 0x0B, 0x0C, 0x0D, 0x0F,
  0x10, 0x11, 0x1F, 0x20, 0x33, 0x40, 0x42
};
static const uint8_t SUPPORTED_01_COUNT = sizeof(SUPPORTED_01) / sizeof(SUPPORTED_01[0]);

// Encode a DTC (type + 4 hex digits, e.g. 'P',0x1668) into its two OBD bytes.
static void encodeDtc(char type, uint16_t code, uint8_t& a, uint8_t& b) {
  uint8_t typeBits;
  switch (type) {
    case 'C': typeBits = 1; break;
    case 'B': typeBits = 2; break;
    case 'U': typeBits = 3; break;
    default:  typeBits = 0; break;   // 'P'
  }
  uint8_t d1 = (code >> 12) & 0x0F;          // first digit after the letter (0..3)
  a = (uint8_t)((typeBits << 6) | ((d1 & 0x03) << 4) | ((code >> 8) & 0x0F));
  b = (uint8_t)(code & 0xFF);
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

String ObdTranslator::handleObd(const String& hexIn) {
  // Normalise: uppercase, strip spaces.
  String h;
  h.reserve(hexIn.length());
  for (size_t i = 0; i < hexIn.length(); i++) {
    char c = hexIn[i];
    if (c == ' ' || c == '\t') continue;
    h += (char)toupper((unsigned char)c);   // (unsigned char) avoids UB on high bytes
  }

  // A trailing single hex digit is the optional "number of responses" hint
  // (e.g. "010C1"). Drop it so we are left with whole bytes.
  if (h.length() & 1) h.remove(h.length() - 1);
  if (h.length() < 2) return OBD_NO_DATA;

  // Parse into bytes (we never need more than a handful).
  uint8_t bytes[8];
  uint8_t n = 0;
  for (size_t i = 0; i + 1 < h.length() && n < 8; i += 2) {
    bytes[n++] = (uint8_t)strtoul(h.substring(i, i + 2).c_str(), nullptr, 16);
  }

  uint8_t mode = bytes[0];
  switch (mode) {
    case 0x01: {
      // A mode-01 request may carry up to 6 PIDs (grouped/"faster" polling).
      // The response is a single "41" followed by each PID's <pid><data>.
      if (n < 2) return OBD_NO_DATA;
      String out = "41";
      bool any = false;
      for (uint8_t i = 1; i < n; i++) {
        String seg = mode01Segment(bytes[i]);
        if (seg.length()) { out += ' '; out += seg; any = true; }
      }
      return any ? out : String(OBD_NO_DATA);
    }

    case 0x03:
      _p.readDTCs();          // refresh snapshot before reporting
      return mode03();

    case 0x04:
      return mode04();

    case 0x07:
      return "47 00";         // no pending codes

    case 0x09:
      return mode09(n >= 2 ? bytes[1] : 0);

    case 0x0A:
      return "4A 00";         // no permanent codes

    case 0x22:
      if (n < 3) return OBD_NO_DATA;
      return mode22((uint16_t)((bytes[1] << 8) | bytes[2]));

    default:
      return OBD_NO_DATA;
  }
}

String ObdTranslator::batteryVoltageString() {
  char t[8];
  snprintf(t, sizeof(t), "%.1fV", _p.data().batteryVolts());
  return String(t);
}

// ---------------------------------------------------------------------------
// Mode 01 - current data
// ---------------------------------------------------------------------------

String ObdTranslator::supportedPidMask(uint8_t base) {
  uint32_t mask = 0;
  for (uint8_t i = 0; i < SUPPORTED_01_COUNT; i++) {
    uint8_t p = SUPPORTED_01[i];
    if (p > base && p <= (uint8_t)(base + 0x20)) {
      uint8_t offset = p - base;             // 1..32
      mask |= (1UL << (32 - offset));        // PID base+1 => MSB
    }
  }
  String body = hx(base);
  body += ' '; body += hx((uint8_t)(mask >> 24));
  body += ' '; body += hx((uint8_t)(mask >> 16));
  body += ' '; body += hx((uint8_t)(mask >> 8));
  body += ' '; body += hx((uint8_t)(mask));
  return body;
}

String ObdTranslator::mode01Segment(uint8_t pid) {
  const VehicleData& d = _p.data();
  String body = hx(pid);

  // ECU disconnected: report NO DATA for live values (empty segment) so the app
  // freezes the last reading instead of showing stale zeros. Support masks
  // (00/20/40) still answer so adapter discovery keeps working.
  if (pid != 0x00 && pid != 0x20 && pid != 0x40 && !_p.connected()) return String("");

  switch (pid) {
    case 0x00:
    case 0x20:
    case 0x40:
      return supportedPidMask(pid);

    case 0x01: {  // Monitor status: MIL + DTC count (readiness monitors = none)
      // Refresh the fault count so the app's check-engine widget is accurate.
      // NOTE Phase 2: Td5Provider::readDTCs() should cache to avoid a K-Line
      // round-trip on every 0101 poll.
      _p.readDTCs();
      uint8_t cnt = _p.data().dtcCount;
      uint8_t a = (cnt ? 0x80 : 0x00) | (cnt & 0x7F);   // bit7 = MIL on
      body += ' '; body += hx(a);
      body += " 00 00 00";
      return body;
    }

    case 0x04:  // Calculated engine load, %
      body += ' '; body += hx(clamp8(lroundf(d.loadPct * 255.0f / 100.0f)));
      return body;

    case 0x05:  // Coolant temp, A-40
      body += ' '; body += hx(clamp8(lroundf(d.coolantC) + 40));
      return body;

    case 0x0B:  // Intake MAP, kPa (absolute)
      body += ' '; body += hx(clamp8(lroundf(d.mapKpa)));
      return body;

    case 0x0C: { // RPM = ((A*256)+B)/4
      uint16_t raw = clamp16((long)d.rpm * 4);
      appendU16(body, raw);
      return body;
    }

    case 0x0D:  // Speed, km/h
      body += ' '; body += hx(clamp8(d.speedKmh));
      return body;

    case 0x0F:  // Intake air temp, A-40
      body += ' '; body += hx(clamp8(lroundf(d.intakeAirC) + 40));
      return body;

    case 0x10: { // MAF, ((A*256)+B)/100 g/s
      uint16_t raw = clamp16(lroundf(d.mafGs * 100.0f));
      appendU16(body, raw);
      return body;
    }

    case 0x11:  // Throttle / driver demand, %
      body += ' '; body += hx(clamp8(lroundf(d.throttlePct * 255.0f / 100.0f)));
      return body;

    case 0x1F: { // Run time since engine start, s
      uint16_t raw = clamp16((long)d.runtimeSec);
      appendU16(body, raw);
      return body;
    }

    case 0x33:  // Absolute barometric pressure, kPa
      body += ' '; body += hx(clamp8(lroundf(d.ambientKpa)));
      return body;

    case 0x42: { // Control module voltage = ((A*256)+B)/1000 V
      uint16_t raw = clamp16((long)d.batteryMv);
      appendU16(body, raw);
      return body;
    }

    default:
      return String("");   // unsupported: empty so the group loop skips it
  }
}

// ---------------------------------------------------------------------------
// Mode 03 / 04 - fault codes
// ---------------------------------------------------------------------------

String ObdTranslator::mode03() {
  const VehicleData& d = _p.data();
  uint8_t n = d.dtcCount;
  if (n > MAX_DTCS) n = MAX_DTCS;

  // CAN-style mode 03: "43 <count> <DTC pairs...>"
  String body = "43 " + hx(n);
  for (uint8_t i = 0; i < n; i++) {
    uint8_t a, b;
    encodeDtc(d.dtcs[i].type, d.dtcs[i].code, a, b);
    body += ' '; body += hx(a);
    body += ' '; body += hx(b);
  }
  return body;
}

String ObdTranslator::mode04() {
  // Reflect the real outcome: "44" (positive) only if the clear actually went
  // through, else NO DATA so the app doesn't falsely report success.
  return _p.clearDTCs() ? String("44") : String(OBD_NO_DATA);
}

// ---------------------------------------------------------------------------
// Mode 09 - vehicle information. VIN (PID 0x02) from the ECU's programming
// history (flash/NNN ECUs only; empty otherwise -> NO DATA).
// ---------------------------------------------------------------------------

String ObdTranslator::mode09(uint8_t pid) {
  const VehicleData& d = _p.data();
  switch (pid) {
    case 0x00:                       // supported PIDs: only 0x02 (VIN)
      return "49 00 40 00 00 00";
    case 0x02: {                     // VIN
      if (d.vin[0] == '\0') return OBD_NO_DATA;
      String body = "49 02 01";      // 01 = one data item follows
      appendAscii(body, d.vin, 17);
      return body;
    }
    default:
      return OBD_NO_DATA;
  }
}

// ---------------------------------------------------------------------------
// Mode 22 - Td5 custom PIDs (ReadDataByIdentifier), DIDs 0xF0xx
// Response: "62 <DID hi> <DID lo> <data...>". Equations live in the shipped
// Td5_Diagnostic_PIDs.csv so the app can decode them.
// ---------------------------------------------------------------------------

String ObdTranslator::mode22(uint16_t did) {
  const VehicleData& d = _p.data();
  String body = "62 " + hx((uint8_t)(did >> 8)) + " " + hx((uint8_t)(did & 0xFF));

  // Live DIDs report NO DATA when the ECU is disconnected (app freezes last value).
  // The static identity strings (F021-F024, read once on connect) still answer.
  if (!_p.connected() && !(did >= 0xF021 && did <= 0xF024)) return OBD_NO_DATA;

  switch (did) {
    case 0xF001:  // Injection quantity, mg  -> ((A*256)+B)/100
      appendU16(body, clamp16(lroundf(d.injectionMg * 100.0f)));
      return body;

    case 0xF002:  // Boost, bar (offset +1) -> (((A*256)+B)/1000)-1
      appendU16(body, clamp16(lroundf((d.boostBar + 1.0f) * 1000.0f)));
      return body;

    case 0xF003:  // Fuel temp, degC        -> A-40
      body += ' '; body += hx(clamp8(lroundf(d.fuelTempC) + 40));
      return body;

    case 0xF004:  // MAF air mass, g/s       -> ((A*256)+B)/100
      appendU16(body, clamp16(lroundf(d.mafGs * 100.0f)));
      return body;

    case 0xF005:  // Torque limit, mg        -> ((A*256)+B)/100
      appendU16(body, clamp16(lroundf(d.torqueLimitMg * 100.0f)));
      return body;

    case 0xF006:  // Smoke limit, mg         -> ((A*256)+B)/100
      appendU16(body, clamp16(lroundf(d.smokeLimitMg * 100.0f)));
      return body;

    case 0xF007:  // EGR position, %         -> ((A*256)+B)/10
      appendU16(body, clamp16(lroundf(d.egrPct * 10.0f)));
      return body;

    case 0xF008:  // Wastegate position, %   -> ((A*256)+B)/10
      appendU16(body, clamp16(lroundf(d.wastegatePct * 10.0f)));
      return body;

    case 0xF009:  // Selected gear           -> A
      body += ' '; body += hx(d.gear);
      return body;

    case 0xF00A:  // Accel track 1, V        -> ((A*256)+B)/1000
      appendU16(body, clamp16(lroundf(d.accelTrack1V * 1000.0f)));
      return body;

    case 0xF00B:  // Accel track 2, V        -> ((A*256)+B)/1000
      appendU16(body, clamp16(lroundf(d.accelTrack2V * 1000.0f)));
      return body;

    case 0xF00C:  // Driver demand, %        -> ((A*256)+B)/10
      appendU16(body, clamp16(lroundf(d.throttlePct * 10.0f)));
      return body;

    case 0xF00D: { // Input switches         -> A (bitwise)
      uint8_t bits = 0;
      if (d.brakePressed)  bits |= 0x01;
      if (d.clutchPressed) bits |= 0x02;
      if (d.cruiseMaster)  bits |= 0x04;
      if (d.cruiseSet)     bits |= 0x08;
      if (d.cruiseResume)  bits |= 0x10;
      if (d.acRequest)     bits |= 0x20;
      if (d.transferHigh)  bits |= 0x40;
      body += ' '; body += hx(bits);
      return body;
    }

    case 0xF00E:  // Accel track 3, V        -> ((A*256)+B)/1000
      appendU16(body, clamp16(lroundf(d.accelTrack3V * 1000.0f)));
      return body;

    case 0xF010:  // Accelerator 5V supply/reference, V -> ((A*256)+B)/1000
      appendU16(body, clamp16((long)d.refVoltageMv));
      return body;

    case 0xF013:  // Roughness cyl 1 (signed int16, RPM)
    case 0xF014:  // Roughness cyl 2
    case 0xF015:  // Roughness cyl 3
    case 0xF016:  // Roughness cyl 4
    case 0xF017:  // Roughness cyl 5
      appendU16(body, (uint16_t)d.injTrim[did - 0xF013]);
      return body;

    case 0xF018:  // Idle speed error, RPM (signed)  -> (int16)((A*256)+B)
      appendU16(body, (uint16_t)d.idleSpeedErrorRpm);
      return body;

    case 0xF019:  // EGR inlet throttle, %           -> ((A*256)+B)/100
      appendU16(body, clamp16(lroundf(d.egrInletPct * 100.0f)));
      return body;

    case 0xF01A: { // Status/relay bitfield (16 bits, see mask below)
      uint16_t bits = 0;
      if (d.mainRelay)        bits |= 0x0001;
      if (d.fuelPumpRelay)    bits |= 0x0002;
      if (d.glowPlugLight)    bits |= 0x0004;
      if (d.glowPlugRelay)    bits |= 0x0008;
      if (d.milOn)            bits |= 0x0010;
      if (d.radFanDrive)      bits |= 0x0020;
      if (d.acClutchDrive)    bits |= 0x0040;
      if (d.ignitionOn)       bits |= 0x0080;
      if (d.securityLinkHigh) bits |= 0x0100;
      appendU16(body, bits);
      return body;
    }

    case 0xF01B:  // Coolant temp sensor voltage, mV  -> ((A*256)+B)
      appendU16(body, clamp16(lroundf(d.coolantSensorV * 1000.0f)));
      return body;
    case 0xF01C:  // Inlet air temp sensor voltage, mV
      appendU16(body, clamp16(lroundf(d.intakeAirSensorV * 1000.0f)));
      return body;
    case 0xF01D:  // Fuel temp sensor voltage, mV
      appendU16(body, clamp16(lroundf(d.fuelTempSensorV * 1000.0f)));
      return body;
    case 0xF01E:  // MAF sensor voltage, mV
      appendU16(body, clamp16(lroundf(d.mafSensorV * 1000.0f)));
      return body;
    case 0xF01F:  // Manifold pressure direct reading -> ((A*256)+B)/100 kPa
      appendU16(body, clamp16(lroundf(d.mapDirectKpa * 100.0f)));
      return body;

    case 0xF021:  // Map / calibration variant (8 ASCII)
      if (d.mapName[0] == '\0') return OBD_NO_DATA;
      appendAscii(body, d.mapName, 8);
      return body;
    case 0xF022:  // Fuel variant (8 ASCII)
      if (d.fuelVariant[0] == '\0') return OBD_NO_DATA;
      appendAscii(body, d.fuelVariant, 8);
      return body;
    case 0xF023:  // Homologation (4 ASCII)
      if (d.homologation[0] == '\0') return OBD_NO_DATA;
      appendAscii(body, d.homologation, 4);
      return body;
    case 0xF024:  // VIN (17 ASCII)
      if (d.vin[0] == '\0') return OBD_NO_DATA;
      appendAscii(body, d.vin, 17);
      return body;

    case 0xF025:  // Avg fuel economy, 10-mile, imperial mpg -> ((A*256)+B)/10
      appendU16(body, clamp16(lroundf(d.avgMpg * 10.0f)));
      return body;
    case 0xF026:  // Instantaneous fuel economy, imperial mpg -> ((A*256)+B)/10
      appendU16(body, clamp16(lroundf(d.instMpg * 10.0f)));
      return body;
    case 0xF027:  // Avg fuel economy, 10-mile, L/100km -> ((A*256)+B)/10
      appendU16(body, clamp16(lroundf(d.avgL100 * 10.0f)));
      return body;
    case 0xF028:  // Trip fuel used (total injected), litres -> ((A*256)+B)/100
      appendU16(body, clamp16(lroundf(d.tripFuelL * 100.0f)));
      return body;

    case 0xF029:  // Battery voltage direct reading, mV -> ((A*256)+B)
      appendU16(body, d.batteryDirectMv);
      return body;
    case 0xF02A:  // Ambient pressure direct reading, kPa -> ((A*256)+B)/100
      appendU16(body, clamp16(lroundf(d.ambientDirectKpa * 100.0f)));
      return body;
    case 0xF02B:  // Accelerator pedal type: number of tracks (2 or 3; 0 = unknown) -> A
      body += ' '; body += hx(d.pedalTracks);
      return body;
    case 0xF02C:  // Gearbox drive neutral (auto box; shares A/C-switch bit, UNVERIFIED) -> A (1=neutral)
      body += ' '; body += hx(d.gearboxNeutral ? 1 : 0);
      return body;

    default:
      return OBD_NO_DATA;
  }
}
