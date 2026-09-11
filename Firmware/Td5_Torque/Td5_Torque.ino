/*
 * Td5_Torque.ino - Land Rover Td5 -> Torque (ELM327-over-WiFi) dongle
 * ===================================================================
 *
 * Presents the ESP32 as a WiFi ELM327 OBD-II adapter so the Torque app (and
 * other ELM327 apps) can display live data and read/clear fault codes.
 *
 *   Phone  --WiFi-->  ESP32 SoftAP "Td5-Torque"  (192.168.0.10:35000)
 *          --TCP-->   WifiElmServer -> Elm327 -> ObdTranslator -> DataProvider
 *
 * Phase is chosen by ONE flag in config.h:
 *   DATA_SOURCE_SIM 1  -> SimProvider  (bench: synthetic data + fake DTCs)
 *   DATA_SOURCE_SIM 0  -> Td5Provider  (vehicle: real K-Line data)  [Phase 2]
 *
 * Board: Seeed XIAO ESP32-S3   FQBN: esp32:esp32:XIAO_ESP32S3
 *
 * See README_SETUP.md for Torque configuration and Td5_Torque_PIDs.csv for the
 * importable custom-PID definitions.
 */

#include "config.h"
#include "vehicle_data.h"
#include "data_provider.h"
#include "obd_pids.h"
#include "elm327.h"

#if ENABLE_WIFI_ELM
  #include "wifi_server.h"
#endif
#if ENABLE_BLE_ELM
  #include "ble_server.h"
#endif

#if !ENABLE_WIFI_ELM && !ENABLE_BLE_ELM
  #error "Enable at least one transport (ENABLE_BLE_ELM or ENABLE_WIFI_ELM) in config.h"
#endif

#if DATA_SOURCE_SIM
  #include "sim_provider.h"
  static SimProvider provider;
#else
  #include "td5_provider.h"     // created in Phase 2
  static Td5Provider provider;
#endif

// The stack above the provider is identical in both phases and both transports.
static ObdTranslator obd(provider);
static Elm327        elm(obd);
#if ENABLE_WIFI_ELM
static WifiElmServer wifiServer(elm, ELM_TCP_PORT);
#endif
#if ENABLE_BLE_ELM
static BleElmServer  bleServer(elm);
#endif

void setup() {
  Serial.begin(DEBUG_BAUD);
  Serial.setTxTimeoutMs(0);   // never block on debug prints if no USB reader attached
  delay(300);
  Serial.println();
  Serial.println(F("=== Td5 -> Torque (ELM327 over WiFi) ==="));
#if DATA_SOURCE_SIM
  Serial.println(F("Data source: SIMULATED (Phase 1 bench)"));
#else
  Serial.println(F("Data source: REAL Td5 ECU (Phase 2)"));
#endif
#if ENABLE_BLE_ELM
  Serial.println(F("Transport: BLE (advertising as \"" BLE_DEVICE_NAME "\")"));
#endif
#if ENABLE_WIFI_ELM
  Serial.println(F("Transport: WiFi AP"));
#endif

  provider.begin();
#if ENABLE_WIFI_ELM
  wifiServer.begin();
#endif
#if ENABLE_BLE_ELM
  bleServer.begin();
#endif
}

void loop() {
  provider.poll();      // refresh live values (non-blocking)
#if ENABLE_WIFI_ELM
  wifiServer.poll();    // service the WiFi ELM327 session
#endif
#if ENABLE_BLE_ELM
  bleServer.poll();     // service the BLE ELM327 session
#endif
}
