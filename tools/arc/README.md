# ARC on the X3

The ARC app loads game rules and level data from `/games/arc/*.bin` on the SD
card. The C++ player handles packs, display and input; C/C++ MicroPython bytecode
and ulab runtimes execute the rules. Sprite transforms, collision, cameras and
large affine rasters run natively. New compatible packs do not require reflashing.

The pinned catalog contains **25 public games and 183 levels**. Private evaluation
games are not available. ARCEngine and these public game sources are MIT licensed;
see [third-party notices](../../lib/ArcPlayer/THIRD_PARTY_NOTICES.md).

## Install and play

1. Flash the complete build, including its partition table. An app-only OTA update
   from an older build does not install the `arc_cache` partition. The unused
   3.375 MiB SPIFFS partition is repurposed at the same address; app, NVS and
   coredump boundaries are unchanged.
2. Copy the release's `games` folder to the SD root, giving paths such as
   `/games/arc/ls20-9607627b.bin`. Keep the accompanying license files.
3. Open **ARC** from Home and choose a game.

Directions move in games that expose directional actions. Click games use a
cursor: directions position it, Confirm clicks. Hold a direction to move the
cursor faster. In games with both movement and clicking, hold Confirm to switch
modes. Back opens the menu, with undo where supported, action 5, restart, level
selection and game selection. Only the current level number is saved; reopening
starts that level fresh. All levels are selectable.

The board preserves its 16 palette indices internally. Sixteen monochrome
patterns and a legend distinguish colors on e-ink; cursor mode shows the selected
cell's palette index. Only the final frame of each action is displayed. Intermediate
rule steps and interface renders still execute. This is a human player, without
benchmark score submission or network access from game rules.

## Build packs

Normal PlatformIO builds use the checked-in runtime sources. To regenerate that
runtime and build packs, use Python 3.12+, a C/C++ compiler, make and git:

```sh
python3 tools/arc/bootstrap.py
python3 tools/arc/prepare.py
python3 tools/arc/compile.py --sources .cache/arc/sources \
  --engine .cache/arc/upstream/engine/arcengine --output .cache/arc/embedded --lazy
python3 tools/arc/pack.py --embedded .cache/arc/embedded \
  --sources .cache/arc/sources \
  --mpy-cross .cache/arc/upstream/micropython/mpy-cross/build/mpy-cross \
  --output .cache/arc/packs
```

`bootstrap.py` pins upstream commits and applies the checked-in patches. It also
builds an optional Unix interpreter; use `--no-host` to skip that. `prepare.py`
fetches the public sources and verifies their SHA-256 digests. The anonymous API
key is used in memory and is not stored in packs. A changed game source requires
an explicit catalog update and fresh compatibility checks.

## Verify

Install the pinned Python engine in a host virtual environment, then run:

```sh
python3 -m pip install .cache/arc/upstream/engine
python3 tools/arc/build_native.py
python3 tools/arc/verify_primitives.py --mpy-cross .cache/arc/upstream/micropython/mpy-cross/build/mpy-cross
python3 tools/arc/verify.py
```

This executes the **actual binary packs** in the same VM configuration as the
firmware, comparing frame CRCs, game state, level and move count against CPython.
Every level runs 24 deterministic actions, including reset and advertised undo,
then closes and reopens the pack. Logs and results go to `.cache/arc/verification`.
These traces cover selected paths, not complete solutions or every possible
action sequence. Host heap statistics depend on pointer size and do not establish
device RAM capacity, latency, display quality or button behavior.

Core pack validation, transforms, collisions and palette patterns are also
covered by the normal CMake host test suite in `test/arc_player`.

CI also builds a 32-bit Linux player with the device's 16 KiB VM stack limit.
The full comparison uses a 2 MiB heap to separate rule compatibility from memory
capacity. A second, informational run measures a 256 KiB heap. The X3's available
contiguous heap must still be measured on hardware; neither host budget is a
claim about the device. On the 64-bit host, BP35 and LF52 require substantially
more memory than the smaller games, including failures in a 512 KiB BP35 probe.
The initial 32-bit run measured up to 456,016 bytes of live VM data, before system
overhead, so the largest levels require further memory work for the X3. LF52's
recursive cover search is lowered to the same ordered search with an explicit
stack to respect the device's call-stack limit.
**All-game playability on the X3 is not yet established.**

## Package for device testing

After `pio run -e default`, assemble separate flash segments and the SD archive:

```sh
python3 tools/arc/release.py --boot-app ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin
```

The archives are in `.cache/arc/release`. `arc-x3-firmware.zip` includes the
application, bootloader, partition table, OTA initializer, licenses, checksums
and a flash command. Separate address segments preserve NVS; no padded full-flash
image is generated. `arc-sd-games.zip` contains the SD `games/arc` directory.
The packaging command does not contact or flash a device.

## Compatibility and memory

This port targets the pinned public catalog; it is not a general CPython/NumPy
implementation. It removes typing and Pydantic overhead, lowers match statements,
implements the random sequences used by the games, and preserves Python's stable
sort and dictionary order. ulab supplies the array subset. Wide integer array
declarations use 16-bit storage for the catalog's small coordinates and colors;
new games using larger ranges need additional support. Desktop debug image export,
arbitrary imports and native machine-code packs are unsupported.

Only one level is materialized at a time. BP35/LF52 undo snapshots use a lossless
compact representation between actions and reconstruct ordinary objects before
access. The game-specific adaptations are source-pinned in `sources.json`.
Long histories can still grow. The worker has a 20 KiB stack, reserves system
headroom and allocates the remaining contiguous heap for the VM. It reports an
error on exhaustion. Actions yield cooperatively and can be canceled with Back;
a 60-second limit prevents a stuck rule loop from holding the app indefinitely.

The selected pack is copied to a flash cache and mapped read-only, so bytecode
and constants do not consume heap. Reopening an identical pack avoids rewriting
flash. Only the current pack is cached. Progress is stored separately on SD.

## Pack format (ABI 1)

All integers are little-endian. Header: 72 bytes, `<8sHHIHH16s32sI`:
`ARCPACK1`, format version, ABI, total bytes, module count, level count, terminated
game ID, upstream source SHA-256, and CRC-32 of the entire body. Each 60-byte
directory entry is `<48sIII`: terminated module path, offset, length and CRC-32.
The payloads are portable MicroPython v6 bytecode with 31-bit small integers.
Native machine code, overlapping/out-of-range entries and invalid checksums are
rejected. Limits: 3 MiB, 64 modules and one `_arc_boot.mpy` entry.

Use packs produced from reviewed sources with this toolchain. CRCs detect copying
errors; they do not authenticate executable rules. The bytecode runtime is not a
security sandbox for hostile packs.
