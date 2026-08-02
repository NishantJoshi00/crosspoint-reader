# RNG App Design

Date: 2026-08-01
Status: Approved

## Summary

A home-menu app that rolls dice or tosses coins. The user picks Coin or Dice,
adjusts the item count (1-6) with the side buttons, and shakes the device to
roll. Randomness comes from `esp_random()` (hardware RNG).

## Decisions (workshopped)

- **Trigger**: shake only, no button fallback. On boards without an IMU
  (plain X4) the roll view shows a "not supported" notice; the app stays in
  the home menu.
- **Display**: faces only — no totals/summary line.
- **Animation**: one fast-refresh scramble frame of random faces, then the
  real result.
- **Icon**: custom 32x32 five-pip die glyph (`UIIcon::Dice`), Lyra mapping,
  same pattern as the QR icon.

## Architecture

- `src/activities/rng/RngActivity.{h,cpp}` — single activity, two screens via
  an internal `enum class Screen { PICKER, ROLL }`. Picker is a 2-row list
  (Coin / Dice). Roll view draws 1-6 faces, orientation-aware, laid out from
  `getScreenWidth()/getScreenHeight()`.
- Home menu: `HomeMenuItem::RNG` inserted after `QR_CODES`, wired through the
  five home-menu integration points plus `ActivityManager::goToRng()`.
- No persistence, normal sleep behavior, zero heap allocations (faces render
  from stack state).

## Shake detection (HAL extension)

`main.cpp` drives `halTiltSensor.update(SETTINGS.tiltPageTurn, ...)` every
loop; with tilt page-turn OFF it force-sleeps the IMU, which would fight an
activity that woke it. Therefore `HalTiltSensor` gains a *shake session*:

- `beginShakeSession()` / `endShakeSession()` — bracket the activity's
  lifetime; wake/sleep the IMU; `update()` early-returns while a session is
  active so the global caller cannot interfere.
- `updateShake()` — 20 Hz poll; total gyro magnitude `sqrt(gx²+gy²+gz²)`
  above 450 dps triggers, then requires return below 80 dps plus a 900 ms
  cooldown before the next trigger (mirrors the tilt flick state machine).
- `wasShaken()` — consumed-on-read event, same idiom as `wasTiltedForward()`.
- A shake sets `_hadActivity`, so the existing inactivity check in `main.cpp`
  keeps the device awake while playing.

## Rendering

- Dice: rounded rects with filled pips (1-6), pip layout from a constexpr
  table.
- Coins: circles with a centered H/T glyph (`tr(STR_RNG_HEADS_ABBR)` /
  `tr(STR_RNG_TAILS_ABBR)`).
- Layout: up to 3 items per row, face size computed from screen dimensions so
  all four orientations work.
- Roll: `loop()` computes final values, sets a scramble flag, and requests a
  render; `render()` draws random throwaway faces, displays with
  `FAST_REFRESH`, waits ~150 ms, then draws and displays the real result.

## Out of scope

Roll history, sounds, more than 6 items, non-6-sided dice, button-triggered
rolls.
