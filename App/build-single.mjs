// build-single.mjs
// Produces a single, self-contained Td5-Diagnostic.html (at the repo root) by
// inlining ble-shim.js into www/index.html. The result needs no other files and
// no hosting: open it in any Web Bluetooth browser (Chrome/Edge on Android or a
// PC), or in Bluefy on iOS. Run with: npm run single
import { readFileSync, writeFileSync } from 'node:fs';

const html = readFileSync(new URL('./www/index.html', import.meta.url), 'utf8');
let shim = readFileSync(new URL('./www/ble-shim.js', import.meta.url), 'utf8');

// Defensive: make sure nothing in the bundle can close the <script> element early.
shim = shim.replace(/<\/script/gi, '<\\/script');

const out = html.replace(
  '<script src="ble-shim.js"></script>',
  '<script>\n' + shim + '\n</script>'
);
if (out === html) {
  console.error('build-single: could not find the ble-shim.js script tag');
  process.exit(1);
}

writeFileSync(new URL('../Td5-Diagnostic.html', import.meta.url), out);
console.log(`build-single: wrote Td5-Diagnostic.html (${out.length} bytes)`);
