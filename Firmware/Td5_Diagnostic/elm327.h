/*
 * elm327.h - Minimal ELM327 command emulator.
 *
 * Consumes one command line at a time (as sent by the app, terminated by CR)
 * and returns the exact bytes an ELM327 would send back, including echo (if
 * enabled), the CR line ending, and the '>' ready prompt.
 *
 * AT commands are handled here; OBD requests are forwarded to ObdTranslator.
 *
 * Power-on / ATZ default flag state (matches a real ELM327):
 *   echo ON, linefeed OFF, headers OFF, spaces ON.
 */

#ifndef TD5_TORQUE_ELM327_H
#define TD5_TORQUE_ELM327_H

#include <Arduino.h>
#include "obd_pids.h"

class Elm327 {
public:
  explicit Elm327(ObdTranslator& obd) : _obd(obd) { reset(); }

  // Restore power-on default flag state.
  void reset();

  // Process one command line (raw, as received, without the trailing CR).
  // Returns the full framed response ready to write to the socket.
  String handleLine(const String& raw);

  bool echoEnabled() const { return _echo; }

private:
  ObdTranslator& _obd;

  bool _echo;
  bool _linefeed;
  bool _headers;
  bool _spaces;
  bool _protoAuto;
  uint8_t _protoNum;   // current protocol number (6 = ISO 15765-4 CAN 11/500)
  String _lastCmd;     // last non-empty command (bare CR repeats it, per ELM327)

  String handleAt(const String& cmd);   // cmd = uppercased, spaces stripped, incl. "AT"
  String eol() const { return _linefeed ? "\r\n" : "\r"; }
  // Wrap a response body with echo (optional) + line ending + prompt.
  String frame(const String& body, const String& echoCmd);
  // Apply CAN framing to an OBD data body ("41 05 7F" / "43 0A ...") per the
  // current ATH/ATS flags: headers-off = reassembled data; headers-on = a valid
  // ISO 15765 single frame (<=7 bytes) or First+Consecutive frames (>7 bytes).
  String frameCan(const String& body);
};

#endif // TD5_TORQUE_ELM327_H
