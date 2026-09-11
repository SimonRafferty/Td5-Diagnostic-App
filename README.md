# Td5-Diagnostic-App
An OBD2 Scanner for Td5 Land Rovers, Compatible with ELM327 with it's own Android App

An OBD2 Scanner has become an essential part of most vehicle owners toolkit.  However, if you own a Land Rover Td5, your options are limited & expensive!

NanoCom from BlackBox Solutions is the go-to for most people - but it is rather expensive.  If you want to re-map your engine, it's good value - but I suspect many users just want to be able to read live data, read and clear Diagnostic Trouble Codes (DTC's).  For any other vehicle, a simple ELM327 scanner and a phone app will costs next to nothing - but Td5 owners have to fork out £600+ for NanoCom.  My intention with this project is to provide a lower cost option.  One that I can leave in the Land Rover without having to worry that it will get wet / damaged / stolen.

Along with the linked hardware (A K-Line Interface + ESP32S3), this emulates an ELM327 scanner when plugged into a Td5 Engine.  Use an app such as 'EOBD-Facile', connected via bluetooth low energy (BLE).  Unfortunately, it is not compatible with Torque which only uses Bluetooth 3 & that's not available on an ESP32S3.

Inside your scanner, you should see a Bluetooth device called "OBDII".  Connect to this.

Alternatively, install the APK in the repo - it's completely free & open source.  Unlike the other OBD2 apps, it will also display all the information unique to Td5 vehicles.  You can easily read and clear DTC fault codes too.

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

Apple's Safari does not support Web Bluetooth, so it will not connect there.  Install the free **Bluefy** browser from the App Store and open **https://simonrafferty.github.io/Td5-Diagnostic-App/Td5-Diagnostic.html**.  

## What the App Does

There are four simple tabs - swipe left and right to move between them:

- **Fault Codes** - read and clear DTC's, each with a plain-English description of the Td5 fault and a link to look it up online.
- **Available Data** - every parameter the ECU will give you, including the extras that are unique to the Td5.  Tick what you'd like to see and/or graph, and pick your units.
- **Live Data** - your chosen parameters, updating live.
- **Graphs** - up to four auto-scaling charts so you can watch how things move (boost, temperatures, injector balance and so on).

## Screenshots

| Fault Codes | Available Data |
|:-----------:|:--------------:|
| ![Fault Codes screen](DTC.jpg) | ![Available Data screen](Available_Data.jpg) |
| **Live Data** | **Graphs** |
| ![Live Data screen](Live_Data.jpg) | ![Graphs screen](Graph.jpg) |

## What's in this Repository

| Folder | Contents |
|--------|----------|
| `Firmware/` | The ESP32-S3 dongle firmware (Arduino).  This is what turns the K-Line interface into a BLE ELM327 that the app - and other OBD2 apps - can talk to. |
| `App/` | The Android app project (built with Capacitor) used to produce the APK. |
| `Hardware/` | Gerber files and PCB details for the K-Line interface. |
| [`Td5-Diagnostics-debug.apk`](Td5-Diagnostics-debug.apk) | The ready-to-install Android app. |
| [`Td5-Diagnostic.html`](Td5-Diagnostic.html) | The whole app as a single, self-contained web page (browser use, or iOS via Bluefy). |

## The Hardware

The dongle is an ESP32-S3 plus a simple K-Line interface (an L9637D transceiver and a little protection circuitry).  It connects to the Td5 diagnostic line - OBD pin 7, or pin B18 on the ECU.  I'll upload the Gerber files and the rest of the PCB details to the **Hardware** folder shortly, so you can have a board made.

## Building From Source

**Firmware:** open `Firmware/Td5_Diagnostic/Td5_Diagnostic.ino` in the Arduino IDE, select the **XIAO_ESP32S3** board and upload.  The only extra library you'll need is *EspSoftwareSerial*.

**App:** the app itself is an HTML file (`App/www/index.html`) plus a small Bluetooth bridge (`App/www/ble-shim.js`).  The bridge is only used by the **native APK** - an Android WebView has no Web Bluetooth, so the bridge hands BLE to the phone's Bluetooth stack via Capacitor.  To rebuild the APK, run `npm install` in the `App` folder, then `npx cap sync android` and build with Android Studio (or Gradle).

**Single-file web page:** [Td5-Diagnostic.html](Td5-Diagnostic.html) is generated from `App/www/index.html` with `npm run single` (run in the `App` folder).  It is a browser-only build, so it drops the `ble-shim.js` bridge and uses the browser's built-in Web Bluetooth directly - which keeps it to a single, lightweight file.

