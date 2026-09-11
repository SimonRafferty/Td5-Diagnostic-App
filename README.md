# Td5-Diagnostic-App
An OBD2 Scanner for Td5 Land Rovers, Compatible with ELM327 with it's own Android App

An OBD2 Scanner has become an essential part of most vehicle owners toolkit.  However, if you own a Land Rover Td5, your options are limited & expensive!

NanoCom from BlackBox Solutions is the go-to for most people - but it is rather expensive.  If you want to re-map your engine, it's good value - but I suspect many users just want to be able to read live data, read and clear Diagnostic Trouble Codes (DTC's).  For any other vehicle, a simple ELM327 scanner and a phone app will cost peanuts - but no such, low cost, device exists for Td5 owners.  My intention with this project is to provide an option.

Along with the linked hardware (A K-Line Interface + ESP32S3), this emulates an ELM327 scanner when plugged into a Td5 Engine.  Use an app such as 'EOBD-Facile', connected via bluetooth low energy (BLE).  Unfortunately, it is not compatible with Torque which only uses Bluetooth 3 & that's not available on an ESP32S3.

Inside your scanner, you should see a Bluetooth device called "OBDII".  Connect to this.

Alternatively, install the APK in the repo - it's completely free & open source.  Unlike the other OBD2 apps, it will also display all the information unique to Td5 vehicles.  You can easily read and clear DTC fault codes too.

## Installing the App

The app isn't on the Google Play Store just yet - I'll upload it there in due course to make installing and updating it a one-tap affair.  In the meantime you can "side-load" it directly, which only takes a minute:

1. On your Android phone or tablet, download **Td5-Diagnostics-debug.apk** from this repository (tap the file in the list above, then the download button).
2. Open it from your Downloads.  Android will warn you it's from an "unknown source" - this is completely normal for any app installed outside the Play Store.
3. Tap through to Settings on that prompt and allow "Install unknown apps" for your browser (or Files app), then go back and tap Install.
4. Open Td5 Diagnostics, tap Connect, and choose "OBDII" from the list.

Because this is a debug build it isn't signed for the Play Store, so that "unknown source" warning is expected and safe to accept.  Once the Play Store version is live you'll be able to install it the usual way.

## On iPhone and iPad

The app is written for Android, but the same code can run on iOS too - with one catch.  Apple's Safari does not support Web Bluetooth (the standard the app uses to talk to the scanner), so it will not connect if you simply open it in Safari.

The work-around is a free Web Bluetooth browser called **Bluefy**, from the App Store:

1. Install Bluefy on your iPhone or iPad.
2. Host the contents of the `App/www` folder somewhere Bluefy can reach it - GitHub Pages is an easy, free option - and open that address in Bluefy.  (Once I've set up a hosted version I'll link it here so you can skip this step.)
3. Tap Connect and choose "OBDII", just as you would on Android.

If you'd rather have a proper native iOS app, the project is built with Capacitor, which also targets iOS: on a Mac with Xcode you can run `npx cap add ios` and build it, as the Bluetooth plugin supports iOS as well.  I may add that in due course.

## What the App Does

There are four simple tabs - swipe left and right to move between them:

- **Fault Codes** - read and clear DTC's, each with a plain-English description of the Td5 fault and a link to look it up online.
- **Available Data** - every parameter the ECU will give you, including the extras that are unique to the Td5.  Tick what you'd like to see and/or graph, and pick your units.
- **Live Data** - your chosen parameters, updating live.
- **Graphs** - up to four auto-scaling charts so you can watch how things move (boost, temperatures, injector balance and so on).

## What's in this Repository

| Folder | Contents |
|--------|----------|
| `Firmware/` | The ESP32-S3 dongle firmware (Arduino).  This is what turns the K-Line interface into a BLE ELM327 that the app - and other OBD2 apps - can talk to. |
| `App/` | The Android app project (built with Capacitor) used to produce the APK. |
| `Hardware/` | Gerber files and PCB details for the K-Line interface.  I'll add these here myself. |
| `Td5-Diagnostics-debug.apk` | The ready-to-install Android app. |

## The Hardware

The dongle is an ESP32-S3 plus a simple K-Line interface (an L9637D transceiver and a little protection circuitry).  It connects to the Td5 diagnostic line - OBD pin 7, or pin B18 on the ECU.  I'll upload the Gerber files and the rest of the PCB details to the **Hardware** folder shortly, so you can have a board made.

## Building From Source

**Firmware:** open `Firmware/Td5_Torque/Td5_Torque.ino` in the Arduino IDE, select the **XIAO_ESP32S3** board and upload.  The only extra library you'll need is *EspSoftwareSerial*.

**App:** the app itself is a single, self-contained HTML file (`App/www/index.html`) wrapped up with Capacitor so it can run natively and use the phone's Bluetooth.  To rebuild the APK, run `npm install` in the `App` folder, then `npx cap sync android` and build with Android Studio (or Gradle).

On a fresh clone there is no `local.properties` file - it just tells Gradle where your machine's Android SDK lives, so it is deliberately left out of the repo.  Android Studio creates it for you automatically when you open the `App` folder, or you can add one yourself containing a single line: `sdk.dir=/path/to/your/Android/Sdk`.
