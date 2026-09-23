/*
 * transport_arbiter.h - one-radio-per-session lock (BLE vs WiFi).
 *
 * The dongle brings up BOTH BLE advertising and the WiFi SoftAP at boot. The
 * first transport to get a REAL app connection "claims" the session; the main
 * loop then shuts the other radio down (see Td5_Diagnostic.ino). The claim is a
 * compare-and-set: whoever finds it T_NONE wins, and the loser is refused.
 *
 * "For this session" = until the next deep-sleep wake, which is a full reboot,
 * so this is just an in-RAM flag with no persistence.
 *
 * claim() is called from two different tasks - the NimBLE host task (on the
 * first BLE command) and the main loop (from the HTTP POST /elm handler) - so
 * the compare-and-set runs inside a portMUX critical section. It is idempotent
 * for the winner: re-claiming the transport you already own returns true, so
 * repeated connect/POST events never spuriously fail.
 */
#ifndef TD5_TRANSPORT_ARBITER_H
#define TD5_TRANSPORT_ARBITER_H

#include <Arduino.h>

enum Transport : uint8_t { T_NONE = 0, T_BLE = 1, T_WIFI = 2 };

class TransportArbiter {
public:
  // Returns true if `t` owns the session after this call (won it now, or already had it).
  bool claim(Transport t) {
    bool ok;
    portENTER_CRITICAL(&_mux);
    if (_active == T_NONE) { _active = t; ok = true; }
    else                   { ok = (_active == t); }
    portEXIT_CRITICAL(&_mux);
    return ok;
  }

  Transport active() {
    portENTER_CRITICAL(&_mux);
    Transport a = _active;
    portEXIT_CRITICAL(&_mux);
    return a;
  }

private:
  volatile Transport _active = T_NONE;
  portMUX_TYPE       _mux    = portMUX_INITIALIZER_UNLOCKED;
};

#endif // TD5_TRANSPORT_ARBITER_H
