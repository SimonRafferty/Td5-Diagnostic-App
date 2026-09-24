/*
 * ble-shim.js  (source; bundled by esbuild -> www/ble-shim.js)
 *
 * Exposes the Capacitor community BLE client on window so the single-file app
 * (index.html) can use it when running inside the packaged Android app. In a
 * plain browser this still loads a "web" platform Capacitor, whose
 * isNativePlatform() returns false, so index.html falls back to Web Bluetooth.
 */
import { BleClient } from '@capacitor-community/bluetooth-le';
window.BleClient = BleClient;
