/*
 * elm327.cpp - Minimal ELM327 command emulator implementation.
 * See elm327.h for the overview.
 */

#include "elm327.h"
#include "config.h"

void Elm327::reset() {
  _echo      = true;    // power-on defaults, per ELM327 datasheet
  _linefeed  = false;
  _headers   = false;
  _spaces    = true;
  _protoAuto = true;
  _protoNum  = 6;       // ISO 15765-4 (CAN 11/500)
  _lastCmd   = "";
}

// 2-char uppercase hex.
static String hx2(uint8_t b) {
  char t[3];
  snprintf(t, sizeof(t), "%02X", b);
  return String(t);
}

String Elm327::frame(const String& body, const String& echoCmd) {
  String out;
  if (_echo && echoCmd.length()) {
    out += echoCmd;
    out += eol();
  }
  out += body;
  out += eol();
  out += ">";
  return out;
}

String Elm327::handleLine(const String& raw) {
  // Interpret and echo the command without its trailing CR/whitespace.
  String cmd = raw;
  cmd.trim();
  if (cmd.length() == 0) {
    // Bare CR: just re-issue the prompt. (We tried "repeat last command" per the
    // ELM327 datasheet, but Car Scanner then received stray OK/duplicate replies
    // and desynced into an ATZ re-init loop and value swapping - so do NOT
    // repeat; a bare CR returns only the '>' prompt.)
    return String(">");
  }

  // Normalised form for matching: uppercase, no spaces.
  String norm;
  norm.reserve(cmd.length());
  for (size_t i = 0; i < cmd.length(); i++) {
    char c = cmd[i];
    if (c == ' ' || c == '\t') continue;
    norm += (char)toupper((unsigned char)c);   // (unsigned char) avoids UB on high bytes
  }

  // Capture echo state BEFORE dispatch: a command like ATE0 must still be
  // echoed (echo was on when it arrived), only *later* commands are not.
  bool echoOn = _echo;

  String body;
  if (norm.startsWith("AT")) {
    body = handleAt(norm);
  } else {
    // OBD hex request?
    bool isHex = norm.length() >= 2;
    for (size_t i = 0; i < norm.length() && isHex; i++) {
      char c = norm[i];
      if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) isHex = false;
    }
    if (isHex) {
      body = _obd.handleObd(norm);
      if (body != OBD_NO_DATA) body = frameCan(body);
    } else {
      body = "?";   // unrecognised
    }
  }

  // Frame using the echo state captured at arrival.
  bool savedEcho = _echo;
  _echo = echoOn;
  String out = frame(body, cmd);
  _echo = savedEcho;
  return out;
}

String Elm327::frameCan(const String& body) {
  // Parse the OBD data body ("41 05 7F", "43 0A 01 15 ...") into raw bytes.
  uint8_t d[96];
  int n = 0;
  for (size_t i = 0; i + 1 < body.length() && n < 96; ) {
    if (body[i] == ' ') { i++; continue; }
    d[n++] = (uint8_t)strtoul(body.substring(i, i + 2).c_str(), nullptr, 16);
    i += 2;
  }

  const char* sep = _spaces ? " " : "";

  // Headers OFF: the ELM327 reassembles and shows just the data bytes.
  if (!_headers) {
    String out;
    for (int k = 0; k < n; k++) { if (k && _spaces) out += ' '; out += hx2(d[k]); }
    return out;
  }

  // Headers ON: emit ISO 15765-4 (CAN 11/500) frames from ECU 0x7E8.
  const char* ID = "7E8";
  if (n <= 7) {
    // Single frame: PCI = 0x0L (L = data length).
    String out = ID; out += sep; out += hx2((uint8_t)n);
    for (int k = 0; k < n; k++) { out += sep; out += hx2(d[k]); }
    return out;
  }

  // Multi-frame: First Frame (PCI 0x1LLL, 6 data bytes) + Consecutive Frames
  // (PCI 0x2S, up to 7 data bytes each, sequence wrapping 1..F,0..).
  String out = ID; out += sep;
  out += hx2((uint8_t)(0x10 | ((n >> 8) & 0x0F)));   // 0x1L (high length nibble)
  out += sep; out += hx2((uint8_t)(n & 0xFF));        // low length byte
  for (int k = 0; k < 6 && k < n; k++) { out += sep; out += hx2(d[k]); }

  int idx = 6;
  uint8_t seq = 1;
  while (idx < n) {
    out += eol();                                    // each frame on its own line
    out += ID; out += sep; out += hx2((uint8_t)(0x20 | (seq & 0x0F)));
    for (int k = 0; k < 7 && idx < n; k++, idx++) { out += sep; out += hx2(d[idx]); }
    seq = (seq + 1) & 0x0F;
  }
  return out;
}

String Elm327::handleAt(const String& cmd) {
  String r = cmd.substring(2);   // strip leading "AT"

  // --- Resets / identity -------------------------------------------------
  if (r == "Z" || r == "WS") { reset(); return ELM_VERSION; }
  if (r == "D")              { reset(); return "OK"; }   // set all to defaults
  if (r == "I")              { return ELM_VERSION; }
  if (r == "@1")             { return ELM_DESCRIPTION; }
  if (r == "@2")             { return "?"; }
  if (r == "RV")             { return _obd.batteryVoltageString(); }

  // --- Protocol describe -------------------------------------------------
  if (r == "DPN") {
    // Return the BARE protocol number (e.g. "6"), NOT "A6". The "A" prefix means
    // "auto, not yet confirmed"; apps (Car Scanner) then keep re-detecting -
    // the 0100<->ATDPN loop we observed. The proven ESP32 emulators reply "6",
    // i.e. protocol locked, so the app accepts it and starts polling.
    String p = String(_protoNum, HEX);
    p.toUpperCase();
    return p;
  }
  if (r == "DP") {
    String p = _protoAuto ? "AUTO, " : "";
    p += "ISO 15765-4 (CAN 11/500)";
    return p;
  }

  // --- Set protocol (must be checked before the generic "S" handler) -----
  if (r.startsWith("SP")) {
    String rest = r.substring(2);
    if (rest.length() == 0) {
      // no-op
    } else if (rest[0] == 'A') {
      _protoAuto = true;
      if (rest.length() > 1) {
        uint8_t v = (uint8_t)strtoul(rest.substring(1).c_str(), nullptr, 16);
        if (v) _protoNum = v;
      }
    } else if (rest == "0") {
      _protoAuto = true;
    } else {
      _protoAuto = false;
      uint8_t v = (uint8_t)strtoul(rest.c_str(), nullptr, 16);
      if (v) _protoNum = v;
    }
    return "OK";
  }

  // Other "S*" commands that are NOT "set spaces".
  if (r.startsWith("ST") || r.startsWith("SH") || r.startsWith("SW") ||
      r.startsWith("SR")) {
    return "OK";
  }

  // --- Simple on/off flags ----------------------------------------------
  if (r.startsWith("S")) { _spaces   = (r == "S1"); return "OK"; }   // spaces
  if (r.startsWith("E")) { _echo     = (r == "E1"); return "OK"; }   // echo
  if (r.startsWith("L")) { _linefeed = (r == "L1"); return "OK"; }   // linefeed
  if (r.startsWith("H")) { _headers  = (r == "H1"); return "OK"; }   // headers

  // --- Adaptive timing (ATAT0/1/2) --------------------------------------
  if (r.startsWith("AT")) return "OK";

  // --- Everything else we simply acknowledge ----------------------------
  if (r.startsWith("M"))  return "OK";                       // memory
  if (r == "PC")          return "OK";                       // protocol close
  if (r.startsWith("CRA") || r.startsWith("CAF") || r.startsWith("CFC") ||
      r.startsWith("CEA") || r.startsWith("CM")  || r.startsWith("CP")  ||
      r.startsWith("CV")  || r.startsWith("CS"))             return "OK";
  if (r.startsWith("PP"))  return "OK";                      // programmable params
  if (r.startsWith("IB") || r.startsWith("IIA"))  return "OK";
  if (r.startsWith("BR"))  return "OK";                      // baud rate
  if (r.startsWith("TP"))  return "OK";                      // try protocol
  if (r == "AL" || r == "NL" || r == "LP" || r == "AR") return "OK";

  // Permissive fallback keeps app init sequences happy.
  return "OK";
}
