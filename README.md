# Td5-Diagnostic-App
An OBD2 Scanner for Td5 Land Rovers, Compatible with ELM327 with it's own Android App

An OBD2 Scanner has become an essential part of most vehicle owners toolkit.  However, if you own a Land Rover Td5, your options are limited & expensive!

NanoCom from BlackBox Solutions is the go-to for most people - but it is rather expensive.  If you want to re-map your engine, it's good value - but I suspect many users just want to be able to read live data, read and clear Diagnostic Trouble Codes (DTC's).  For any other vehicle, a simple ELM327 scanner and a phone app will costs next to nothing - but Td5 owners have to fork out £600+ for NanoCom.  My intention with this project is to provide a lower cost option.  One that I can leave in the Land Rover without having to worry that it will get wet / damaged / stolen.

Along with the linked hardware (a K-Line interface + ESP32-S3), it plugs into the Td5 diagnostic socket and emulates an ELM327 scanner.  Unlike the other OBD2 apps, the app here also shows all the information unique to Td5 vehicles, and reads and clears DTC fault codes - completely free and open source.

## Connecting to the Dongle

There are two ways to talk to the dongle - **WiFi** (the easy route; works on everything, including iPhones) or **Bluetooth** (handy when you want to keep your phone's internet).

### WiFi - recommended (Android, PC and iPhone/iPad)

The dongle hosts the whole app itself, so **any device with a browser** can use it - nothing to install, no Bluetooth, and it works on iOS where browsers aren't allowed to use Bluetooth.

1. Connect your phone, tablet or PC to the open WiFi network **`OBDII`** (no password).
2. Open a browser and go to **http://10.0.0.1**.

That's it.  (While you're on the dongle's WiFi your device has no internet, so online fault-code look-ups won't work - use Bluetooth below if you need those.)

### Bluetooth (BLE) - when you want to keep your internet

Connecting over Bluetooth leaves your phone on its normal WiFi/mobile data, so online DTC look-ups keep working.  Two ways:

- **The Android app** - install the [APK](Td5-Diagnostics-debug.apk) (see *Installing the App* below), tap **Connect**, and choose **OBDII**.
- **The web page over Bluetooth** - open [the app page](https://simonrafferty.github.io/Td5-Diagnostic-App/Td5-Diagnostic.html) in **Chrome or Edge** (Android/PC), or in the free **Bluefy** browser on **iPhone/iPad**, and tap **Connect**.

Generic BLE OBD apps (e.g. EOBD-Facile) work too - look for a Bluetooth device called **OBDII**.  (Torque isn't supported: it needs classic Bluetooth, which the ESP32-S3 doesn't have.)

> The dongle serves **one connection at a time** - whichever you connect with first (WiFi or Bluetooth) is used for that session, and the other is disabled until the dongle restarts (it restarts each time the engine starts).

## Installing the App

The app isn't on the Google Play Store yet - I'll upload it there in due course to make installing and updating it easy.  In the meantime you can "side-load" it directly, which only takes a minute:

1. On your Android phone or tablet, download [**Td5-Diagnostics-debug.apk**](Td5-Diagnostics-debug.apk) from this repository (open the link, then tap the download button on that page).
2. Open it from your Downloads.  Android will warn you it's from an "unknown source" - this is completely normal for any app installed outside the Play Store.
3. Tap through to Settings on that prompt and allow "Install unknown apps" for your browser (or Files app), then go back and tap Install.
4. Open Td5 Diagnostics, tap Connect, and choose "OBDII" from the list.

Because this is a debug build it isn't signed for the Play Store, so that "unknown source" warning is expected and safe to accept.  Once the Play Store version is live, I'll link to it from here.  Unfortunately it takes Google an age to verify & approve an app.

## Running it in a Web Browser - Android or PC

The whole app is packaged as a single, self-contained web page - [**Td5-Diagnostic.html**](Td5-Diagnostic.html) - so it runs in any Chromium browser with Web Bluetooth (Chrome or Edge on Android or a PC).  Two ways to use it:

- **Open it from a file.**  Download [Td5-Diagnostic.html](Td5-Diagnostic.html) onto your phone or PC and open it in Chrome.  Then you will have a completely local copy you can access without internet connectivity.
- **Tap a hosted link.**  Just click the link below (Chrome or Edge only) and the app will load in your browser.  Obviously, you will need internet for this.
  **https://simonrafferty.github.io/Td5-Diagnostic-App/Td5-Diagnostic.html**.

### iPhone and iPad

The easiest route on an iPhone or iPad is **WiFi** - join the **`OBDII`** network and open **http://10.0.0.1** (see *Connecting to the Dongle* above); Safari handles that fine.  Safari can't use Web *Bluetooth*, so if you'd rather connect over Bluetooth (to keep your internet), install the free **Bluefy** browser and open [the app page](https://simonrafferty.github.io/Td5-Diagnostic-App/Td5-Diagnostic.html).  

## What the App Does

There are four simple tabs - swipe left and right to move between them:

- **Fault Codes** - read and clear DTC's, each with a plain-English description of the Td5 fault and a link to look it up online.  Once connected it also shows the vehicle's VIN and current fuel-map name (where the ECU provides them).
- **Available Data** - every parameter the ECU will give you, including the extras that are unique to the Td5 - injector balance, accelerator tracks, EGR and wastegate, glow-plug and relay states, sensor voltages, idle-speed error and more - plus calculated values like turbo boost and live fuel economy (instantaneous, a rolling 10-mile average, and trip fuel used).  Tick what you'd like to see and/or graph, and pick your units.
- **Live Data** - your chosen parameters, updating live.
- **Graphs** - up to four auto-scaling charts so you can watch how things move (boost, temperatures, injector balance and so on).

### Logging

On the **Available Data** tab there's a single **Log displayed items to CSV** tick box.  Tick it and everything you've chosen to display is written to a timestamped CSV file, in your selected units - handy for looking at a fault after a drive, or comparing readings over time.

- **In the app**, the file is saved straight to the phone's **Documents** folder as `Td5_Log_<date>_<time>.csv` (open it with the Files app, or copy it off over USB).
- **In the browser version**, tick to start, then use **Download log now** - or simply untick - to save the file to your browser's Downloads.

## Screenshots

| Fault Codes | Available Data |
|:-----------:|:--------------:|
| ![Fault Codes screen](DTC.jpg) | ![Available Data screen](Available_Data.jpg) |
| **Live Data** | **Graphs** |
| ![Live Data screen](Live_Data.jpg) | ![Graphs screen](Graph.jpg) |

## What's in this Repository

| Folder | Contents |
|--------|----------|
| `Firmware/` | The ESP32-S3 dongle firmware (Arduino).  It turns the K-Line interface into an ELM327 the app - and other OBD2 apps - can talk to over **Bluetooth**, and also hosts the app over **WiFi** (open `OBDII` network, http://10.0.0.1). |
| `App/` | The Android app project (built with Capacitor) used to produce the APK. |
| `Hardware/` | The complete dongle design: PCB gerbers, schematic, BOM, pick-and-place, Altium source, and 3D-printable case halves. |
| [`Td5-Diagnostics-debug.apk`](Td5-Diagnostics-debug.apk) | The ready-to-install Android app. |
| [`Td5-Diagnostic.html`](Td5-Diagnostic.html) | The whole app as a single, self-contained web page (browser use, or iOS via Bluefy). |

## The Hardware

The dongle is an ESP32-S3 plus a simple K-Line interface (an L9637D transceiver and a little protection circuitry).  It connects to the Td5 diagnostic line - OBD pin 7, or pin B18 on the ECU.  Everything needed to build one is in the [`Hardware`](Hardware) folder:

- **PCB** - send [`Td5_OBD2_Gerber.zip`](Hardware/Td5_OBD2_Gerber.zip) to any board house to have the board made.
- **Assembly** - [`Td5_OBD2_BOM.csv`](Hardware/Td5_OBD2_BOM.csv) (bill of materials) and [`Td5_OBD2_PickAndPlace.xlsx`](Hardware/Td5_OBD2_PickAndPlace.xlsx).
- **Design source** - the full [schematic (PDF)](Hardware/Td5_OBD2_Schematic.pdf) and the editable [Altium project](Hardware/Td5_OBD2_Altium.zip).
- **Enclosure** - two 3D-printable case halves: [left](Hardware/Dongle%203D%20Print%20Shell%20Left.STEP) and [right](Hardware/Dongle%203D%20Print%20Shell%20Right.STEP) (STEP).

## Building From Source

**Firmware:** open `Firmware/Td5_Diagnostic/Td5_Diagnostic.ino` in the Arduino IDE, select the **XIAO_ESP32S3** board and upload.  You'll need the *EspSoftwareSerial* and *NimBLE-Arduino* libraries (WiFi and DNSServer are built into the ESP32 core).  The web app the dongle serves over WiFi is embedded as `webapp_html.h`, which is already in the repo - so it builds as-is.  If you edit `Td5-Diagnostic.html`, regenerate that header first with `python Firmware/tools/embed_webapp.py`, then rebuild.

**App:** the app itself is an HTML file (`App/www/index.html`) plus a small Bluetooth bridge (`App/www/ble-shim.js`).  The bridge is only used by the **native APK** - an Android WebView has no Web Bluetooth, so the bridge hands BLE to the phone's Bluetooth stack via Capacitor.  To rebuild the APK, run `npm install` in the `App` folder, then `npx cap sync android` and build with Android Studio (or Gradle).

**Single-file web page:** [Td5-Diagnostic.html](Td5-Diagnostic.html) in the repo is ready to use as-is - just download and open it (see "Running it in a Web Browser" above); nothing to build or install.  It's only mentioned here for completeness: it's generated from `App/www/index.html`, and if you *change the app's source* you can regenerate it by running `npm run single` in the `App` folder.  Being a browser-only build it drops the `ble-shim.js` bridge and uses the browser's built-in Web Bluetooth directly, which keeps it to a single, lightweight file.

