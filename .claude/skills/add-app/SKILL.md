---
name: add-app
description: End-to-end workflow for adding a new app/feature to the firmware — a new activity (screen), flow, or user-facing capability. Use this whenever the task adds a user-facing surface of any kind, even if the words "app" or "activity" never appear - "add a feature", "build a tool for X", "new screen", "new menu entry", "let the user do Y on the device", or extending an existing flow with a new screen. Covers the questions to settle with the user first, the activity model, UI toolkit idioms, integration points (home menu, persistence, sleep/wake), i18n, and the build/test gate. Not for pure refactors or changes inside an existing screen.
---

# Adding an App to CrossPoint

An "app" here is one or more Activities plus their wiring. This skill is the
workflow; it assumes nothing about what the app does. Load companions as they
apply: `scope-discipline` (always, before writing code), `heap-discipline`
(any allocation), `hal-and-abstractions` (storage/input/rendering),
`control-flow-clarity` (modes/state machines), `refactor-for-review` (before
handoff).

## Phase 0 — Settle the design with the user

Ask deliberately, before writing code. One batch, only the questions the
request leaves open:

1. **Scope gate** — does this serve the reading experience enough to justify
   its RAM/flash cost? (Run `scope-discipline`. If it fails the gate, say so
   and offer the no-code alternative.)
2. **Entry point** — home menu item, settings submenu, inside the reader, or
   headless? Home menu real estate is scarce; default to the least prominent
   place that works.
3. **Persistence** — none / a user setting (`SETTINGS`) / runtime state
   (`APP_STATE`) / files on SD? Prefer plain files on SD for user-manageable
   data (they get WebDAV/file-transfer management for free).
4. **Sleep behavior** — normal sleep screen, keep-current-screen-with-moon
   (reader-style), resume-into-the-app after wake, or prevent auto-sleep
   entirely? Each is a different integration (see references/integration.md).
5. **Destination** — upstream PR (must pass the scope gate and review) or
   personal-fork feature (consider a build-flag gate in
   `platformio.local.ini`)?

Do not proceed past open answers on 1, 2, or 5; 3 and 4 can default to
"none"/"normal" if the user has no opinion.

## Phase 1 — Read before writing

Non-negotiable reads (recalled signatures drift; these are short):

- `src/activities/Activity.h` and `src/activities/ActivityManager.h` — the
  lifecycle and navigation contract.
- One existing activity closest in shape to what you're building. Good
  templates: `settings/OpdsServerListActivity` (list + keyboard chaining),
  `reader/QrDisplayActivity` (minimal fullscreen viewer), `home/HomeActivity`
  (icon button menu).
- `docs/activity-manager.md` if the flow nests or returns results.

Details, when you reach them: [references/activity-model.md](references/activity-model.md)
(lifecycle, navigation, result chaining, keyboard entry),
[references/ui-toolkit.md](references/ui-toolkit.md) (drawing, lists, text,
input idioms and their gotchas).

## Phase 2 — Build

- New activities live in `src/activities/<app>/`, named `<Thing>Activity.{h,cpp}`.
- All user-facing strings through `tr()`; all rendering through `GUI`; all
  input through logical `MappedInputManager::Button`; all SD I/O through
  `Storage`/`HalFile`. No exceptions to any of these.
- Wire the entry point and any persistence/sleep integration per
  [references/integration.md](references/integration.md) — the home-menu
  dual-mapping and the boot-routing chain both have ordering traps.
- i18n keys and their generator have sharp edges; follow
  [references/shipping.md](references/shipping.md) exactly.

## Phase 3 — Verify

Run the full gate in [references/shipping.md](references/shipping.md):
regenerate i18n, clang-format 21+ (new files need explicit formatting — the
fix script only touches tracked files), `pio run` to zero project-source
warnings, then hand the user a concrete on-device test checklist covering
every flow you added, sleep/wake if touched, and a heap sanity check. State
plainly what you verified (build) and what only hardware can verify.
