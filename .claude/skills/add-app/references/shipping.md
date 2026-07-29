# Shipping: i18n, Formatting, Build, Verification

## i18n workflow (sharp edges)

1. Add `STR_*` keys to `lib/I18n/translations/english.yaml` **only**. Other
   languages fall back to English automatically; do not pad them.
2. The file is generator-parsed, not real YAML: **no comment lines** between
   entries — `# ...` anywhere after the header makes the generator error out.
   Strictly `KEY: "value"` lines.
3. Regenerate: `python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/`.
   The build also regenerates, but run it yourself so compilation and the
   check below happen now.
4. Read the generator's summary: it cross-references keys against code and
   reports "Never used". Adding keys before referencing them in code is fine
   mid-work, but the final state must report 0 unused.
5. `I18nKeys.h` / `I18nStrings.{h,cpp}` are generated and gitignored — never
   edit, never commit.

## Formatting

- The repo requires clang-format **21+** (`.clang-format` uses v21 features;
  `./bin/clang-format-fix` enforces the version). No local binary?
  `pip install "clang-format~=21.1"` provides one.
- Trap: `clang-format-fix` formats `git ls-files` output, so **untracked new
  files are silently skipped**. Run
  `clang-format -style=file -i <each new file>` explicitly, or `git add -N`
  them first.

## Build gate

- `pio run -e default` must succeed with **zero errors and zero warnings in
  project sources** (`src/`, project `lib/`). Third-party warnings (wolfSSL
  redefines, ESP-IDF components) pre-exist — filter your grep, don't chase
  them.
- No `pio` on the machine: install into a venv. The espressif platform
  requires **Python ≥ 3.10** (a 3.9 system Python installs PlatformIO fine,
  then fails at platform setup). First build downloads the toolchain —
  minutes, once.
- Check the final `RAM:` / `Flash:` lines and report deltas if your feature
  moved them meaningfully. Flash sits near capacity; a big jump is a review
  topic, not a footnote.
- Fork-local features: gate with a `-D` flag in `platformio.local.ini`
  (gitignored), `#ifdef` around the activity and its wiring hooks.

## Git hygiene

Before staging anything: `git status` and confirm no generated files
(`*.generated.h`, `I18nKeys.h`, `.pio/`, `compile_commands.json`,
`platformio.local.ini`) are included. Commit only when the user asks; branch
off `develop` first if on it.

## Verification and hand-off

You can verify: the build gate above, i18n generation, formatting, and (by
inspection) orientation-awareness of any custom drawing. You cannot verify
behavior on hardware — say so plainly and hand the user a checklist built
from what you actually changed. Template:

1. Flash: `pio run -t upload`, then open the monitor
   (`python3 scripts/debugging_monitor.py`).
2. Walk **every flow you added** — each menu path, each wizard branch
   including cancel-at-every-step, empty states (no items yet), and the
   overwrite/duplicate paths.
3. If you touched sleep/wake: let the idle timeout fire on your screen,
   confirm the expected sleep screen, wake, confirm the landing screen; also
   test the Back-held-during-wake escape.
4. If you drew custom layouts: check all four orientations.
5. Heap: log `ESP.getFreeHeap()` after repeated enter/exit cycles of your
   activity; it must return to baseline (leak check) and stay above ~50KB.
6. If you persisted anything: power-cycle (not just sleep) and confirm state
   survives; corrupt/delete the persisted file and confirm graceful fallback.

Close by restating what the user chose in Phase 0 and confirming each choice
shipped as agreed.
