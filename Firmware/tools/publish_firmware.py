#!/usr/bin/env python3
"""
publish_firmware.py - build the dongle firmware and drop it into the web flasher.

The web flasher (flash/index.html, served by GitHub Pages) installs whatever is in
flash/firmware/ using the version in flash/manifest.json. This script refreshes both:

  1. Picks the version: the one given on the command line, else today's date
     (YYYY-MM-DD; a second release on the same day becomes YYYY-MM-DD.2, .3 ...).
  2. Stamps it into config.h (FW_VERSION - the dongle reports it via AT@2 and the
     app shows it at the bottom of the screen).
  3. Re-embeds the web app (embed_webapp.py), so the dongle serves the current app.
  4. Compiles with arduino-cli against the Arduino IDE's sketchbook libraries, so the
     build matches what the IDE produces.
  5. Copies bootloader / partition table / boot_app0 / app into flash/firmware/ and
     writes flash/manifest.json.

It never commits or pushes - review, then commit and push to make the update live:

    python publish_firmware.py            # version = today's date
    python publish_firmware.py 2026-11-01 # explicit version

Environment overrides: ARDUINO_CLI (path to arduino-cli), ARDUINO_USER_DIR (the IDE
sketchbook folder - the one containing 'libraries').
"""
import datetime, glob, json, os, re, shutil, subprocess, sys, tempfile

HERE      = os.path.dirname(os.path.abspath(__file__))
ROOT      = os.path.dirname(os.path.dirname(HERE))          # repo root (Firmware/tools/..)
SKETCH    = os.path.join(ROOT, 'Firmware', 'Td5_Diagnostic')
CONFIG_H  = os.path.join(SKETCH, 'config.h')
FLASH_DIR = os.path.join(ROOT, 'flash')
FW_DIR    = os.path.join(FLASH_DIR, 'firmware')
MANIFEST  = os.path.join(FLASH_DIR, 'manifest.json')
FQBN      = 'esp32:esp32:XIAO_ESP32S3'

# Flash layout of the default_8MB partition scheme (what the IDE writes).
PARTS = [  # (file in flash/firmware/, offset)
    ('bootloader.bin', 0x0000),
    ('partitions.bin', 0x8000),
    ('boot_app0.bin',  0xE000),
    ('firmware.bin',   0x10000),
]


def die(msg):
    raise SystemExit('publish_firmware: ' + msg)


def find_arduino_cli():
    p = os.environ.get('ARDUINO_CLI') or shutil.which('arduino-cli')
    if p:
        return p
    hits = sorted(glob.glob(os.path.expanduser('~/Downloads/arduino-cli*/arduino-cli*')))
    hits = [h for h in hits if os.path.isfile(h)]
    if hits:
        return hits[-1]
    die('arduino-cli not found - put it on PATH or set ARDUINO_CLI')


def find_boot_app0():
    roots = ['~/AppData/Local/Arduino15', '~/.arduino15', '~/Library/Arduino15']
    hits = []
    for r in roots:
        hits += glob.glob(os.path.expanduser(
            r + '/packages/esp32/hardware/esp32/*/tools/partitions/boot_app0.bin'))
    if not hits:
        die('boot_app0.bin not found - is the esp32 core installed?')
    return sorted(hits)[-1]


def pick_version(requested):
    if requested:
        return requested
    today = datetime.date.today().isoformat()
    current = None
    if os.path.exists(MANIFEST):
        with open(MANIFEST, encoding='utf-8') as f:
            current = json.load(f).get('version')
    if current is None or not current.startswith(today):
        return today
    # Same-day re-release: 2026-10-05 -> 2026-10-05.2 -> 2026-10-05.3 ...
    m = re.fullmatch(re.escape(today) + r'(?:\.(\d+))?', current)
    n = int(m.group(1)) if (m and m.group(1)) else 1
    return '%s.%d' % (today, n + 1)


def stamp_config(version):
    with open(CONFIG_H, 'rb') as f:
        src = f.read().decode('utf-8')
    new, n = re.subn(r'(#define FW_VERSION\s+)"[^"]*"', r'\g<1>"%s"' % version, src)
    if n != 1:
        die('expected exactly one FW_VERSION define in config.h, found %d' % n)
    with open(CONFIG_H, 'wb') as f:
        f.write(new.encode('utf-8'))


def main():
    version = pick_version(sys.argv[1] if len(sys.argv) > 1 else None)
    print('version:', version)
    stamp_config(version)

    subprocess.run([sys.executable, os.path.join(HERE, 'embed_webapp.py')], check=True)

    env = dict(os.environ)
    env['ARDUINO_DIRECTORIES_USER'] = os.environ.get(
        'ARDUINO_USER_DIR', os.path.expanduser('~/Documents/Arduino/Sketchbook'))
    build = tempfile.mkdtemp(prefix='td5_publish_')
    print('compiling (libraries from %s) ...' % env['ARDUINO_DIRECTORIES_USER'])
    r = subprocess.run([find_arduino_cli(), 'compile', '--fqbn', FQBN,
                        '--output-dir', build, SKETCH], env=env)
    if r.returncode != 0:
        die('compile failed')

    os.makedirs(FW_DIR, exist_ok=True)
    sources = {
        'bootloader.bin': os.path.join(build, 'Td5_Diagnostic.ino.bootloader.bin'),
        'partitions.bin': os.path.join(build, 'Td5_Diagnostic.ino.partitions.bin'),
        'boot_app0.bin':  find_boot_app0(),
        'firmware.bin':   os.path.join(build, 'Td5_Diagnostic.ino.bin'),
    }
    for name, src in sources.items():
        if not os.path.exists(src):
            die('missing build output ' + src)
        shutil.copyfile(src, os.path.join(FW_DIR, name))
    shutil.rmtree(build, ignore_errors=True)

    manifest = {
        'name': 'Td5 Diagnostic dongle',
        'version': version,
        'new_install_prompt_erase': False,     # never offer to erase: keeps stored MPG
        'new_install_improv_wait_time': 0,     # firmware has no Improv - don't wait for it
        'builds': [{
            'chipFamily': 'ESP32-S3',
            'parts': [{'path': 'firmware/' + n, 'offset': o} for n, o in PARTS],
        }],
    }
    with open(MANIFEST, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(manifest, f, indent=2)
        f.write('\n')

    print('published %s to %s' % (version, os.path.relpath(FLASH_DIR, ROOT)))
    print('next: check it, then commit + push - the flasher page updates about a minute later.')


if __name__ == '__main__':
    main()
