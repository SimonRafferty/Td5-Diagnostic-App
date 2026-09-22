/*
 * config.h - Build-time configuration for the Td5 Diagnostic ELM327 dongle (V2)
 *
 * ONE FLAG controls the whole build phase:
 *   DATA_SOURCE_SIM 1  -> Phase 1 (bench): synthetic data, no ECU needed
 *   DATA_SOURCE_SIM 0  -> Phase 2 (vehicle): real Td5 K-Line data
 *
 * V2 adds comparator-gated deep sleep for the new PCB (see bottom of file).
 * Everything else (WiFi, ELM327 emulation, OBD translation) is identical to V1.
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
#define AP_SSID           "Td5-Diagnostic"
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

// ---------------------------------------------------------------------------
// V2 hardware: power management (comparator-gated deep sleep)
// ---------------------------------------------------------------------------
// The V2 PCB adds a battery-sense divider (D3) and a TLV3691 comparator (D4)
// whose output goes HIGH when Vbat > ~13.5V (engine started / alternator
// charging). The dongle deep-sleeps when the engine is off and wakes on start.
//   ENABLE_DEEP_SLEEP 1 -> wake on engine start (D4 HIGH), sleep when engine off
//   ENABLE_DEEP_SLEEP 0 -> stay awake always (bench, or V1 hardware without the
//                          comparator - D4 would float and could sleep at random)
//
// Pin assignments CONFIRMED on the V2 PCB (2026-09-20): D3=GPIO4, D4=GPIO5.
#define ENABLE_DEEP_SLEEP        1

#define PIN_VBAT_SENSE           GPIO_NUM_4   // D3 - ADC1_CH3, battery divider tap (47k/10k)
#define PIN_CHG_SENSE            GPIO_NUM_5   // D4 - RTC-capable, TLV3691 out / EXT0 wake
#define INACTIVITY_TIMEOUT_MS    (5UL * 60UL * 1000UL)  // no ECU comms + D4 low => sleep

// D3 battery-sense scaling & Serial debug (never affects the OBD/ELM data)
#define ENABLE_VBAT_DEBUG        1            // 0 to drop the Serial print entirely
#define VBAT_DIVIDER_RATIO       5.7f         // Vbat = Vadc * (47+10)/10
#define VBAT_READ_INTERVAL_MS    2000
#define VBAT_ADC_SAMPLES         8

// Voltage-based sleep thresholds. The sleep DECISION always uses the (accurate) D3
// battery reading; only the WAKE mechanism depends on the comparator being healthy.
#define CHARGE_MV                13000        // Vbat above this = engine running/charging -> stay awake
                                              //   (13.0V not 13.5V: heavy loads e.g. a heated screen sag a
                                              //    RUNNING engine below 13.5V; engine-off rests ~11.8-12.2V)
#define LOW_BATT_MV              12500        // clearly parked/off-charge; a healthy D4 must read LOW here
#define SHORT_SLEEP_US           (10ULL * 1000000ULL)         // comparator can't wake us -> poll every 10s
#define LONG_SLEEP_US            (5ULL * 60ULL * 1000000ULL)  // healthy comparator wakes us; long safety backstop

#endif // TD5_TORQUE_CONFIG_H
