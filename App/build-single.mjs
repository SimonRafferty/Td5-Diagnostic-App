// build-single.mjs
// Produces a single, self-contained Td5-Diagnostic.html (at the repo root) for use
// in a Web Bluetooth browser (Chrome/Edge on Android or a PC, or Bluefy on iOS).
//
// The browser talks straight to navigator.bluetooth, so the native ble-shim.js
// bridge - which is only needed inside the Capacitor APK (Android WebViews have no
// Web Bluetooth) - is stripped out here to keep the standalone file small.
// Run with: npm run single
import { readFileSync, writeFileSync } from 'node:fs';

const REPLACEMENT =
  '<!-- Standalone browser build: talks straight to the Web Bluetooth API. The\n' +
  '     native ble-shim.js bridge is only needed inside the Capacitor APK. -->';

const html = readFileSync(new URL('./www/index.html', import.meta.url), 'utf8');

// Remove the native BLE bridge (its comment block + the <script> tag). Fall back to
// stripping just the tag if the surrounding comment ever changes.
let out = html.replace(
  /<!--\s*Native BLE bridge[\s\S]*?-->\s*<script src="ble-shim\.js"><\/script>/,
  REPLACEMENT
);
if (out === html) {
  out = html.replace('<script src="ble-shim.js"></script>', REPLACEMENT);
}
if (out === html) {
  console.error('build-single: could not find the ble-shim.js script tag');
  process.exit(1);
}

writeFileSync(new URL('../Td5-Diagnostic.html', import.meta.url), out);
console.log(`build-single: wrote Td5-Diagnostic.html (${out.length} bytes)`);
