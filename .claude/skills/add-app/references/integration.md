# Integration Points: Home Menu, Persistence, Sleep/Wake

## Home menu entry

Five places must change together; missing one compiles fine and misbehaves at
runtime:

1. `HomeMenuItem` enum in `ActivityManager.h` — insert your item at its menu
   position (order matters for the mappings below).
2. `HomeActivity.h` — **both** `menuItemToIndex()` and `indexToMenuItem()`.
   They are mirror-image if/increment chains and must stay in the same order,
   including the conditional OPDS entry handling.
3. `HomeActivity::getMenuItemCount()` — bump the base count.
4. `HomeActivity::render()` — add your `tr(...)` label and `UIIcon` to the
   `menuItems`/`menuIcons` vectors at the same position.
5. `activateSelection` switch in `HomeActivity::loop()` — dispatch to a new
   `ActivityManager::goToYourApp()` wrapper.

When leaving your top-level screen with Back, call
`activityManager.goHome(HomeMenuItem::YOURS)` so home restores the selection
to your entry.

`UIIcon` (BaseTheme.h) is a fixed set implemented per-theme; adding a value
means drawing it in every theme. Reuse the closest existing icon unless the
user asks for custom art.

## Persistence: pick the lightest tier

- **None** — most screens. Session state dies with the activity; that is a
  feature.
- **User setting** — `CrossPointSettings` (`SETTINGS`). Only for choices the
  user expects to persist. Guard every save with a value-change check and
  never save per interaction (flash wear; CLAUDE.md rule 8).
- **Runtime state** — `CrossPointState` (`APP_STATE`), persisted to
  `/.crosspoint/state.json`. Add a member, then mirror it in `toJson()` and
  `fromJson()` using the `doc["key"] | defaultValue` pattern (silently
  tolerates older state files — that IS the migration strategy). Saved
  automatically on sleep; call `saveToFile()` yourself at other decision
  points.
- **SD files, one file per item** — the pattern for user-visible collections
  (see `src/util/QrStore` end to end). Filename = display name, content =
  payload. Users get create/delete/rename for free through the file-transfer
  web UI and their computer; you get trivial listing via directory scan.
  Cap sizes, strip trailing newlines on load (computer-authored files), and
  sanitize names before using them as filenames. All I/O through
  `Storage`/`HalFile`.

Binary formats need version fields and bumps — see the cache-versioning rules
in CLAUDE.md before inventing one; prefer text/JSON unless size forces binary.

## Sleep and wake

Mental model: deep sleep is a **chip reset**. Nothing in RAM survives.
"Resuming" means persisting intent before sleep and re-routing at boot.

The flow, all in `src/main.cpp` and `SleepActivity`:

1. `enterDeepSleep()` captures what-was-on-screen facts into `APP_STATE`
   (e.g. `lastSleepFromReader`, `sleepQrName`), saves state, then swaps in
   `SleepActivity`.
2. `SleepActivity::onEnter()` picks the sleep screen. The e-ink panel retains
   whatever was last displayed at zero power, so "keep the current screen"
   is simply *not clearing it*: `renderLastScreenSleepScreen()` stamps the
   moon icon bottom-left over the existing framebuffer and refreshes. Add an
   early branch keyed on your persisted state if your app should keep its
   screen regardless of the user's configured sleep screen.
3. Wake reboots; the boot routing if/else chain near the end of `setup()`
   dispatches. Its order is deliberate: recovery mode → panic report →
   silent-reboot targets → (resume branches) → home fallback → reader
   resume. Insert your resume branch before the home fallback (which would
   otherwise swallow it).

Two conventions your resume branch must follow (the reader established both):

- **Crash-loop guard:** copy the persisted intent, clear it, `saveToFile()`,
  *then* enter your activity. If the activity panics, the next boot has
  nothing to resume into.
- **Escape hatch:** skip the resume when
  `mappedInputManager.isPressed(Button::Back)` — holding Back during wake
  always reaches home.

To detect "my activity was on screen" at sleep time, follow the
`qrSleepName()` pattern: a virtual on `Activity` returning data-or-null,
forwarded by a one-liner on `ActivityManager`, called from
`enterDeepSleep()`. Staleness is impossible if `enterDeepSleep()`
unconditionally overwrites the state field every sleep (empty when your
activity is not current).

An activity that must *never* auto-sleep (server, long transfer) overrides
`preventAutoSleep()` instead — but battery cost is why almost nothing does.

## Background work

FreeRTOS task rules are in CLAUDE.md (stack sizes in bytes, create in
`onEnter`, `vTaskDelete` in `onExit` before destruction, mutex-protect shared
state). Before reaching for a task, check whether chunked work in `loop()`
suffices — most "background" needs on a single-core chip are really just
"don't block longer than a tick".
