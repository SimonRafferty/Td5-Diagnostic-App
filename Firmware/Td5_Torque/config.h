/*
 * config.h - Build-time configuration for the Td5 -> Torque ELM327 dongle
 *
 * ONE FLAG controls the whole build phase:
 *   DATA_SOURCE_SIM 1  -> Phase 1 (bench): synthetic data, no ECU needed
 *   DATA_SOURCE_SIM 0  -> Phase 2 (vehicle): real Td5 K-Line data
 *
 * Everything else (WiFi, ELM327 emulation, OBD translation) is identical in
 * both phases - only the DataProvider implementation changes.
 */

#ifndef TD5_TORQUE_CONFIG_H
#define TD5_TORQUE_CONFIG_H

// ---------------------------------------------------------------------------
// Phase selector
// ---------------------------------------------------------------------------
#define DATA_SOURCE_SIM   0        // 1 = simulated (bench), 0 = real Td5 ECU

// ---------------------------------------------------------------------------
// Transport selector - how the phone talks to the dongle
// ---------------------------------------------------------------------------
// BLE keeps the phone on its normal WiFi/cellular internet (so online DTC
// lookups work); the WiFi AP does not. You can enable either or both, but for
// the "keep internet" goal run BLE-only (the phone must NOT join the AP).
//   BLE  works with Car Scanner / OBD Fusion (BLE-native) and *maybe* Torque.
//   WiFi is the guaranteed-Torque fallback (192.168.0.10:35000).
#define ENABLE_BLE_ELM    1        // 1 = advertise as a BLE ELM327
#define ENABLE_WIFI_ELM   0        // 1 = also run the WiFi AP + TCP server

#define BLE_DEVICE_NAME   "OBDII"  // generic name every OBD app recognises

// ---------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------
#define DEBUG_SERIAL      1        // 1 = print ELM traffic + status to USB serial
#define DEBUG_BAUD        115200
#define TD5_DEBUG_FRAMES  1        // Phase 2 only: log every raw ECU response frame
                                   // (sub-PID + bytes) to USB serial. Set 0 once
                                   // live values are validated on the vehicle.

// ---------------------------------------------------------------------------
// WiFi Access Point (the phone joins this network)
// ---------------------------------------------------------------------------
// Torque's WiFi OBD default is IP 192.168.0.10, port 35000. We match that so
// the user only has to pick "Bluetooth/WiFi -> WiFi" in Torque with defaults.
#define AP_SSID           "Td5-Torque"
#define AP_PASS           "landrover"    // >= 8 chars for WPA2; "" for an open AP
#define AP_CHANNEL        6
// Number of WiFi *associations* the AP allows. Must be >1 so a debug PC and the
// phone can both be joined; the single-session nature of ELM327 is enforced at
// the TCP layer (see WifiElmServer), NOT by limiting WiFi stations.
#define AP_MAX_CLIENTS    4

#define ELM_AP_IP_0       192
#define ELM_AP_IP_1       168
#define ELM_AP_IP_2       0
#define ELM_AP_IP_3       10             // ESP32 will be 192.168.0.10
#define ELM_TCP_PORT      35000

// ---------------------------------------------------------------------------
// ELM327 identity (what the emulator reports for ATZ / ATI)
// ---------------------------------------------------------------------------
// v1.5 is the safest, most widely-compatible version string for Torque.
#define ELM_VERSION       "ELM327 v1.5"
#define ELM_DESCRIPTION   "OBDII to RS232 Interpreter"

// ---------------------------------------------------------------------------
// Phase 2 (real ECU) - K-Line pins on the XIAO ESP32-S3
// (Reused verbatim from the Td5_ESPNow project. Unused when DATA_SOURCE_SIM.)
// ---------------------------------------------------------------------------
#if !DATA_SOURCE_SIM
  #define KLINE_TX_PIN    2      // D1 on silkscreen (via 1k to L9637D)
  #define KLINE_RX_PIN    1      // D0 on silkscreen (direct from L9637D)
#endif

#endif // TD5_TORQUE_CONFIG_H
