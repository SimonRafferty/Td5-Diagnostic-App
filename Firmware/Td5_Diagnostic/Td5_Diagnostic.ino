/*
 * Td5_Diagnostic_V2.ino - Land Rover Td5 Diagnostic (ELM327) dongle - V2 hardware
 * ===============================================================================
 *
 * Identical to Td5_Diagnostic EXCEPT for the V2 PCB power management. The ECU /
 * ELM327 / BLE / OBD stack is UNCHANGED - keep it in sync with Td5_Diagnostic;
 * any protocol/decode change must be applied to BOTH sketches.
 *
 * V2 power management (comparator-gated deep sleep, ported from Td5_ESPNow_V2):
 *   - D3 (GPIO4) = battery-divider sense (ADC, x5.7). Serial-debug only.
 *   - D4 (GPIO5) = TLV3691 comparator output, HIGH when Vbat > ~13.5V
 *                  (engine started / alternator charging). EXT0 deep-sleep wake.
 *   - Wake  : deep sleep -> D4 rising HIGH (engine start).
 *   - Sleep : after INACTIVITY_TIMEOUT_MS (5 min) of no ECU comms AND D4 == LOW.
 *   The dongle wakes on ENGINE START, not ignition-on alone (~12.4V keeps D4 LOW).
 *
 * Pin assignments CONFIRMED on the V2 PCB (2026-09-20): D3=GPIO4 (PIN_VBAT_SENSE),
 * D4=GPIO5 (PIN_CHG_SENSE) - see config.h.
 * Set ENABLE_DEEP_SLEEP 0 in config.h for bench work on hardware without the
 * comparator (a floating D4 could otherwise trigger a random sleep).
 *
 * Board: Seeed XIAO ESP32-S3   FQBN: esp32:esp32:XIAO_ESP32S3
 */

#include "config.h"
#include "vehicle_data.h"
#include "data_provider.h"
#include "obd_pids.h"
#include "elm327.h"

#if ENABLE_DEEP_SLEEP
  #include <esp_sleep.h>          // deep sleep + wake-cause
  #include "driver/rtc_io.h"      // rtc_gpio_* (wake-pin pull config)
#endif

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
  #include "td5_provider.h"
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

// ============================================================================
// V2 POWER MANAGEMENT (comparator-gated deep sleep)
// ============================================================================
#if ENABLE_DEEP_SLEEP

// RTC memory survives deep sleep; reset only on a true power cycle.
RTC_DATA_ATTR uint32_t bootCount = 0;

// Single activity timestamp driving the sleep decision. Reset on ECU comms / charging.
static unsigned long lastActivityMs = 0;

// Power-management runtime state (all session-only; nothing persisted to flash).
static uint16_t g_vbatMv    = 0;      // latest measured battery voltage (mV), refreshed ~2s
static bool     g_fromSleep = false;  // this boot woke from deep sleep (vs a true power-on)
static bool     g_compOk    = false;  // D4 reads LOW while off-charge (healthy) - diagnostic/display only

// Battery voltage on D3 (GPIO4) via the 47k/10k divider, in millivolts.
// Diagnostic / Serial only - never enters the OBD data path.
static uint16_t readBatteryVoltage() {
  uint32_t acc = 0;
  for (uint8_t i = 0; i < VBAT_ADC_SAMPLES; i++)
    acc += analogReadMilliVolts(PIN_VBAT_SENSE);   // eFuse-calibrated mV at the pin
  return (uint16_t)(((float)acc / VBAT_ADC_SAMPLES) * VBAT_DIVIDER_RATIO);
}

// Called only once the engine is judged off (Vbat < CHARGE_MV, ECU quiet). The WAKE
// source is decided from the LIVE comparator level read here:
//   D4 LOW  -> healthy: wake on the D4 rising edge (engine start) + a long backstop
//   D4 HIGH -> can't trust it (stuck-high fault, or genuinely charging): poll in 10s
static void enterSleepMode() {
#if ENABLE_WIFI_ELM
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
#endif
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);   // clean slate - RTC config survives sleep

  if (digitalRead(PIN_CHG_SENSE) == LOW) {
    Serial.printf("\n*** Deep sleep (Boot #%u) -> wake on engine start (D4 rising), %lumin backstop ***\n",
                  bootCount, (unsigned long)(LONG_SLEEP_US / 60000000ULL));
    Serial.flush();
    esp_sleep_enable_ext0_wakeup(PIN_CHG_SENSE, 1);   // D4 is LOW now -> fires on the LOW->HIGH edge
    rtc_gpio_pullup_dis(PIN_CHG_SENSE);               // TLV3691 is push-pull; no internal pull
    rtc_gpio_pulldown_dis(PIN_CHG_SENSE);
    esp_sleep_enable_timer_wakeup(LONG_SLEEP_US);      // safety backstop only
  } else {
    Serial.printf("\n*** Deep sleep (Boot #%u) -> re-check in %lus (D4 HIGH, can't wake on it) ***\n",
                  bootCount, (unsigned long)(SHORT_SLEEP_US / 1000000ULL));
    Serial.flush();
    esp_sleep_enable_timer_wakeup(SHORT_SLEEP_US);
  }

  esp_deep_sleep_start();   // ESP32 resets on wake - never returns
}

// Sleep after INACTIVITY_TIMEOUT_MS of no ECU comms AND battery voltage below the
// charge threshold. The decision uses the accurate D3 reading, not the comparator.
static void checkSleepCondition() {
  // Stay awake while the alternator is charging (engine running) or the ECU is
  // still responding; keep the inactivity window fresh so a full timeout is only
  // needed once the engine actually stops.
  if (g_vbatMv >= CHARGE_MV || provider.connected()) {
    lastActivityMs = millis();
    return;
  }
  if (millis() - lastActivityMs > INACTIVITY_TIMEOUT_MS) {
    Serial.println(F("\n*** Engine off (Vbat low) + inactivity timeout - sleeping ***"));
    enterSleepMode();  // does not return
  }
}

static void vbatDebug() {
  static unsigned long lastVbatMs = 0;
  if (millis() - lastVbatMs >= VBAT_READ_INTERVAL_MS) {
    lastVbatMs = millis();
    uint16_t mv = readBatteryVoltage();
    g_vbatMv = mv;
    int d4 = digitalRead(PIN_CHG_SENSE);

    // Track comparator health for the diagnostic display only: when clearly parked
    // (off-charge), a healthy D4 reads LOW. Session-only, not persisted; the sleep
    // path reads the comparator live rather than trusting this flag.
    if (mv > 1000 && mv < LOW_BATT_MV) {
      bool ok = (d4 == LOW);
      if (ok != g_compOk) {
        g_compOk = ok;
        Serial.printf("[PWR] comparator %s off-charge -> %s wake\n",
                      ok ? "reads LOW (healthy)" : "stuck HIGH (faulty)",
                      ok ? "D4-edge" : "10s poll");
      }
    }

#if ENABLE_VBAT_DEBUG
    Serial.printf("[VBAT] D3: %u mV (%.2f V) | D4: %s | comp: %s | ECU: %s\n",
                  mv, mv / 1000.0f,
                  d4 ? "HIGH" : "low",
                  g_compOk ? "healthy" : "faulty",
                  provider.connected() ? "connected" : "-");
#endif
  }
}

static void powerMgmtSetup() {
  pinMode(PIN_CHG_SENSE, INPUT);                      // comparator out - external push-pull, no pulls
  pinMode(PIN_VBAT_SENSE, INPUT);
  analogSetPinAttenuation(PIN_VBAT_SENSE, ADC_11db);  // full-scale ~0..3.3V
  analogReadResolution(12);

  bootCount++;
  esp_sleep_wakeup_cause_t wake = esp_sleep_get_wakeup_cause();
  g_fromSleep = (wake == ESP_SLEEP_WAKEUP_TIMER || wake == ESP_SLEEP_WAKEUP_EXT0);
  g_vbatMv = readBatteryVoltage();

  const char* wc = wake == ESP_SLEEP_WAKEUP_TIMER ? "TIMER"
                 : wake == ESP_SLEEP_WAKEUP_EXT0  ? "EXT0 (engine start)"
                                                  : "power-on / reset";
  Serial.printf("Boot #%u - wake: %s - Vbat %.2fV\n", bootCount, wc, g_vbatMv / 1000.0f);
  lastActivityMs = millis();   // start the inactivity window
}

#endif // ENABLE_DEEP_SLEEP

void setup() {
  Serial.begin(DEBUG_BAUD);
  Serial.setTxTimeoutMs(0);   // never block on debug prints if no USB reader attached
  delay(300);
  Serial.println();
  Serial.println(F("=== Td5 Diagnostic (ELM327) - V2 (deep sleep) ==="));
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

#if ENABLE_DEEP_SLEEP
  powerMgmtSetup();
  // Woke from deep sleep but still off-charge (parked): go straight back to sleep
  // without spinning up BLE / K-line, to keep average power low.
  if (g_fromSleep && g_vbatMv < CHARGE_MV) {
    Serial.println(F("Woke off-charge -> back to sleep"));
    enterSleepMode();  // does not return
  }
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
  provider.poll();      // refresh live values (non-blocking; K-line runs in its own task)
#if ENABLE_WIFI_ELM
  wifiServer.poll();    // service the WiFi ELM327 session
#endif
#if ENABLE_BLE_ELM
  bleServer.poll();     // service the BLE ELM327 session
#endif
#if ENABLE_DEEP_SLEEP
  vbatDebug();          // periodic Serial battery print (diagnostic only)
  checkSleepCondition();// may enter deep sleep (does not return)
#endif
}
