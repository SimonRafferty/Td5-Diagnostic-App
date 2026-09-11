# Td5 → OBD app (ELM327 emulator) — Setup Guide

This firmware turns the XIAO ESP32-S3 dongle into an **ELM327 OBD-II adapter**
that OBD apps connect to over **BLE** or **WiFi**, to show live data and
read/clear fault codes.

- **Phase 1 (this build):** simulated data + fake fault codes — no ECU needed.
- **Phase 2 (later):** flip one flag to read the real Td5 ECU over K-Line.

## Transport: BLE vs WiFi (choose in `config.h`)

| | BLE (`ENABLE_BLE_ELM 1`) | WiFi (`ENABLE_WIFI_ELM 1`) |
|---|---|---|
| Phone keeps internet | **Yes** (online DTC lookup works) | No (phone joins the dongle AP) |
| Best apps | Car Scanner, OBD Fusion (BLE-native); Torque *maybe* | Torque (rock-solid), any ELM327 app |
| Advertises as | `OBDII` (service FFF0 / FFF1 notify / FFF2 write) | SSID `Td5-Torque` @ 192.168.0.10:35000 |

**Default build = BLE only** (so the phone stays online). Set `ENABLE_WIFI_ELM 1`
(and `ENABLE_BLE_ELM 0`) to fall back to the guaranteed-Torque WiFi path. You can
enable both, but for the keep-internet goal run BLE-only and do **not** join the AP.

> **Use Car Scanner ELM OBD2 for BLE (confirmed working, vehicle-tested).** It is
> BLE-native, keeps the phone online for web DTC lookup, and imports the Td5
> custom PIDs.
>
> **Torque does NOT work over BLE on this board.** Torque uses Bluetooth *Classic*
> (SPP) for Bluetooth adapters, and the XIAO ESP32-S3 has no Classic radio — only
> BLE. Torque will list `OBDII` and say "Connecting… Not responding". For Torque,
> use the **WiFi build** (`ENABLE_WIFI_ELM 1` / `ENABLE_BLE_ELM 0`), or move the
> firmware to an original ESP32 (WROOM-32) that has Bluetooth Classic SPP.
>
> **Run ONE OBD app at a time.** An Android OBD app keeps the BLE link alive via a
> background service even after you swipe it away, blocking a second app. To
> switch apps, **Force-Stop** the first (Android → Settings → Apps → *app* → Force
> stop) or toggle Bluetooth.

---

## BLE mode (default build)

1. **Validate with nRF Connect first** (recommended). Scan → connect to `OBDII` →
   service `FFF0` → enable notifications on `FFF1` → write `0100\r` (hex `30 31 30 30 0D`)
   to `FFF2` → you should get a notification ending in `>`. This proves the GATT
   layer before involving any OBD app.
2. **Car Scanner ELM OBD2** (recommended): Settings → Connection → **ELM327 BLE**
   → scan → pick `OBDII`. Adapter connects; gauges, Read/Clear codes work, and the
   phone keeps its internet for online DTC descriptions.
3. **Torque**: not supported over BLE on this board (Classic-SPP only — see note
   above). Use the WiFi build for Torque.
4. **Custom Td5 PIDs**: the mode-22 DIDs in `Td5_Torque_PIDs.csv` (e.g. `22F001`
   injection qty, `22F002` boost) can be imported (OBD Fusion) or hand-entered
   (Car Scanner) using the same Mode+PID and equations listed there.

---

## 1. Build & upload

Board: **Seeed XIAO ESP32-S3** (`esp32:esp32:XIAO_ESP32S3`).

```bash
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 Td5_ESPNow_Torque/Td5_Torque
arduino-cli upload  --fqbn esp32:esp32:XIAO_ESP32S3 -p <PORT> Td5_ESPNow_Torque/Td5_Torque
```

Open the serial monitor at **115200** — it prints the SoftAP status and every
ELM327 command/response, which is invaluable while debugging.

---

## 2. PC smoke test (do this first)

Before touching the phone, prove the emulator answers correctly:

1. On a PC, join the WiFi network **`Td5-Torque`** (password `landrover`).
2. Open a raw TCP connection to `192.168.0.10` port `35000`, e.g.
   - `ncat 192.168.0.10 35000`  (or PuTTY → Raw → 192.168.0.10 : 35000)
3. Type these (each followed by Enter) and check the replies:

   | You send | Expected reply |
   |----------|----------------|
   | `ATZ`    | `ELM327 v1.5` |
   | `ATE0`   | `OK` |
   | `0100`   | `41 00 98 3B 80 03` (supported-PID bitmask) |
   | `0101`   | `41 01 82 00 00 00` (MIL on + 2 stored codes) |
   | `010C`   | `41 0C xx yy` (RPM) |
   | `010D`   | `41 0D xx` (speed) |
   | `010C0D11` | `41 0C xx yy 0D xx 11 xx` (grouped multi-PID) |
   | `03`     | `43 02 01 15 16 68` (two demo DTCs) |
   | `04`     | `44` (clear) |
   | `0101`   | `41 01 00 00 00 00` (MIL off, 0 codes) |
   | `03`     | `43 00` (none after clear) |

Every reply ends with a `>` prompt. If this works, Torque will too.

---

## 3. Torque configuration

1. On the phone, join WiFi **`Td5-Torque`** (password `landrover`).
   *(The phone loses internet while on this network — that's normal; Torque
   works fully offline.)*
2. Torque → **Settings → OBD2 Adapter Settings → Connection Type → WiFi**.
3. Set the WiFi OBD address to **`192.168.0.10`** and port **`35000`**
   (these are Torque's defaults, so usually no change is needed).
4. Back out; Torque connects automatically. You should see the standard gauges
   (RPM, speed, coolant, etc.) come alive.

### Import the Td5 custom PIDs

To see Td5-specific data (injection quantity, boost, EGR, etc.):

1. Copy **`Td5_Torque_PIDs.csv`** to the phone (e.g. into `…/.torque/extendedpids/`).
2. Torque → **Settings → Manage extra PIDs/Sensors → menu → Add predefined set**
   (or **Import**) → select the CSV.
3. Add the new sensors to a dashboard.

### Read / clear fault codes

- Torque → **Fault Codes** → **Read Codes** shows the active DTCs.
  - `P0115` maps to a standard code, so Torque shows its own description.
  - `P1668` is Land-Rover-specific; Torque shows the code (full Td5 text is in
    the firmware's DTC table in Phase 2 and in the serial log).
- **Clear Codes** clears them; a re-read shows none.

---

## 4. How it fits together

```
Phone (Torque)
   │  WiFi + TCP 35000, ELM327 ASCII
   ▼
WifiElmServer  →  Elm327 (AT commands)  →  ObdTranslator (OBD PIDs/DTCs)
                                              │
                                              ▼
                                     DataProvider  ← the only part that changes
                                       ├─ SimProvider   (Phase 1)
                                       └─ Td5Provider    (Phase 2, K-Line)
```

Data is exchanged through a single `VehicleData` snapshot in engineering units,
so the OBD layer never deals with Td5 raw scaling.

---

## 5. Moving to Phase 2 (real vehicle)

1. Copy `td5comm.cpp/.h`, `keygen.h`, `td5_parameters.h` from the `Td5_ESPNow`
   project into this folder.
2. Add `td5_provider.cpp/.h` and `td5_dtc_table.h`.
3. Set `DATA_SOURCE_SIM 0` in `config.h`.
4. Wire K-Line to the ECU (OBD pin 7 / ECU B18) via the L9637D.

> **Known risk:** running the WiFi radio while bit-banging K-Line at 10400 baud
> can jitter the software serial. If live data is corrupt on the vehicle, move
> K-Line to a hardware UART (the ESP32-S3 UART supports 10400 baud). See the
> project plan for details.

---

## Defaults (change in `config.h`)

| Setting | Value |
|---------|-------|
| SSID | `Td5-Torque` |
| Password | `landrover` (set `AP_PASS ""` for an open AP) |
| IP | `192.168.0.10` |
| Port | `35000` |
| ELM version reported | `ELM327 v1.5` |
| Protocol reported | ISO 15765-4 (CAN 11/500) |
