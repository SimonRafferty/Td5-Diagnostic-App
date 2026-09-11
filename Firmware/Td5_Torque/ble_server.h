/*
 * ble_server.h - BLE ELM327 transport (NimBLE).
 *
 * Advertises the de-facto BLE ELM327 GATT layout (the OBDLink CX / clone
 * profile that Car Scanner, OBD Fusion and Torque probe for):
 *   Service  FFF0
 *   FFF1     Notify   (adapter -> app)
 *   FFF2     Write / Write-No-Response (app -> adapter)
 *   Name     "OBDII"
 *
 * Design note (2026-09-10 rewrite): commands are answered SYNCHRONOUSLY inside
 * the write callback - a byte arrives, we assemble the CR-terminated line, run
 * the shared Elm327, and notify the reply before the callback returns. This
 * mirrors the proven ESP32-S3 emulators (terrafirma2021, ELMulator): request in
 * -> response out, in the same call, on the same task. The previous design
 * pushed bytes to a ring and answered later from the main loop; that cross-task
 * hand-off delayed each reply by a scheduler hop, so under fast/burst polling
 * the app paired replies to the WRONG request (alternating values, ATDPN/0100
 * detection loop, missed DTC read). Answering inline removes that skew.
 *
 * Safe because the K-line runs in its own core-0 task; the OBD layer here only
 * READS the buffered VehicleData snapshot, never drives the wire. NimBLE
 * serialises GATT callbacks, so the line assembler needs no lock.
 */

#ifndef TD5_TORQUE_BLE_SERVER_H
#define TD5_TORQUE_BLE_SERVER_H

#include "config.h"
#if ENABLE_BLE_ELM

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "elm327.h"

class BleElmServer {
public:
  explicit BleElmServer(Elm327& elm) : _elm(elm) {}

  bool begin();
  void poll();     // no-op: commands are answered synchronously in onRxBytes()

  // Called from NimBLE callback (host-task) context:
  void onRxBytes(const uint8_t* data, size_t len);   // assemble + answer inline
  void onConnectEvt(uint16_t connHandle);
  void onDisconnectEvt(uint16_t connHandle);
  void onMtu(uint16_t mtu);

private:
  Elm327&               _elm;
  NimBLEServer*         _server = nullptr;
  NimBLECharacteristic* _notify = nullptr;

  volatile bool     _connected = false;
  volatile uint16_t _mtu       = 23;     // ATT default until negotiated
  volatile uint16_t _curConn   = 0xFFFF; // active connection handle (0xFFFF = none)

  String _line;    // command assembler (host-task only; NimBLE serialises callbacks)

  void notifyChunked(const String& s);
};

#endif // ENABLE_BLE_ELM
#endif // TD5_TORQUE_BLE_SERVER_H
