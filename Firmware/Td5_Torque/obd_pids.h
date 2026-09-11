/*
 * obd_pids.h - OBD-II translation layer.
 *
 * Turns an OBD request (mode + PID, as hex, e.g. "010C") into the ASCII-hex
 * response body an ELM327 would return (e.g. "41 0C 1A F8"), reading values
 * from the DataProvider snapshot. The ELM327 layer above adds echo/prompt/
 * spacing; this layer only produces the space-separated hex payload.
 *
 * Supported:
 *   Mode 01  live data + supported-PID bitmasks (00/20/40)
 *   Mode 03  read stored DTCs
 *   Mode 04  clear DTCs
 *   Mode 07  pending DTCs (always none)
 *   Mode 09  vehicle info (VIN) - minimal
 *   Mode 0A  permanent DTCs (always none)
 *   Mode 22  Td5-specific "custom PIDs" (ReadDataByIdentifier), DIDs 0xF0xx
 *
 * We present the physical protocol as ISO 15765-4 (CAN 11/500) because that is
 * the format Torque and most apps handle most robustly; the app has no way to
 * know the real link is K-Line, and CAN mode 03 gives a clean count byte.
 */

#ifndef TD5_TORQUE_OBD_PIDS_H
#define TD5_TORQUE_OBD_PIDS_H

#include <Arduino.h>
#include "data_provider.h"

// Special sentinel returned when a request cannot be answered.
extern const char* OBD_NO_DATA;   // "NO DATA"

class ObdTranslator {
public:
  explicit ObdTranslator(DataProvider& provider) : _p(provider) {}

  // Handle one OBD request given as a compact hex string (no spaces),
  // e.g. "010C", "03", "22F001". Returns the response body WITH spaces,
  // e.g. "41 0C 1A F8", or OBD_NO_DATA.
  String handleObd(const String& hex);

  // For the ELM327 "ATRV" command: e.g. "12.4V".
  String batteryVoltageString();

private:
  DataProvider& _p;

  // Returns one mode-01 PID's "<pid> <data...>" segment (WITHOUT the leading
  // "41"), or "" if the PID is unsupported. handleObd() prepends a single "41"
  // and concatenates segments so grouped multi-PID requests are answered too.
  String mode01Segment(uint8_t pid);
  String mode03();
  String mode04();
  String mode09(uint8_t pid);
  String mode22(uint16_t did);

  // Build the 4-byte "supported PIDs" segment for a base (0x00/0x20/0x40)
  // as "<base> AA BB CC DD" (no leading "41").
  String supportedPidMask(uint8_t base);
};

#endif // TD5_TORQUE_OBD_PIDS_H
