#!/usr/bin/env python3
"""Package the X3 firmware segments and SD game packs without flashing hardware."""
import argparse
import hashlib
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', type=Path, default=ROOT / '.pio/build/default')
parser.add_argument('--packs', type=Path, default=ROOT / '.cache/arc/packs')
parser.add_argument('--boot-app', type=Path, required=True)
parser.add_argument('--output', type=Path, default=ROOT / '.cache/arc/release')
args = parser.parse_args()
firmware = args.output / 'firmware'
sd = args.output / 'sd/games/arc'
for directory in (firmware, sd):
    directory.mkdir(parents=True, exist_ok=True)
for name in ('bootloader.bin', 'partitions.bin', 'firmware.bin'):
    shutil.copyfile(args.build / name, firmware / name)
shutil.copyfile(args.boot_app, firmware / 'boot_app0.bin')
packs = list(args.packs.glob('*.bin'))
if not packs:
    raise RuntimeError('No game packs found; build them first')
for path in packs + [args.packs / 'games.json']:
    shutil.copyfile(path, sd / path.name)
for directory in (firmware, sd):
    shutil.copytree(ROOT / 'tools/arc/licenses', directory / 'licenses', dirs_exist_ok=True)
    shutil.copyfile(ROOT / 'lib/ArcPlayer/THIRD_PARTY_NOTICES.md', directory / 'THIRD_PARTY_NOTICES.md')
(firmware / 'FLASH.txt').write_text('''X3 / ESP32-C3 firmware segments. Hardware testing is still required.

Install esptool in your Python environment, connect the device, and run from
this directory (replace PORT with its serial port):

python3 -m esptool --chip esp32c3 --port PORT --baud 460800 write-flash --flash-mode dio --flash-size 16MB 0x0 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin

The separate segments preserve the NVS sectors between 0x9000 and 0xe000.
Do not merge them into a padded image that overwrites those sectors.
The partition table is required on the first ARC installation. App-only OTA
will leave the old partition label and ARC will ask for a complete flash.

Copy the SD archive's games folder to the SD root, then open ARC from Home.
''')
for directory, archive in ((firmware, 'arc-x3-firmware.zip'), (args.output / 'sd', 'arc-sd-games.zip')):
    paths = sorted(path for path in directory.rglob('*') if path.is_file() and path.name != 'SHA256SUMS')
    (directory / 'SHA256SUMS').write_text(''.join(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + path.relative_to(directory).as_posix() + '\n' for path in paths))
    with zipfile.ZipFile(args.output / archive, 'w', zipfile.ZIP_DEFLATED) as output:
        for path in sorted(directory.rglob('*')):
            if path.is_file():
                output.write(path, path.relative_to(directory))
    print(args.output / archive)
