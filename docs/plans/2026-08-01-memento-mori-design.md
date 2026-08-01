# Memento Mori Sleep Screen Design

Date: 2026-08-01
Status: Approved

## Summary

A new sleep screen mode that renders the current year of your life as a
52-box week grid: filled boxes are full weeks elapsed since your last
birthday, empty boxes are the weeks remaining until the next one. Below the
grid, the total number of days lived — a bare number, no label.

## Decisions (workshopped)

- **Grid**: exactly 52 boxes (weeks of the current age-year), 13x4,
  black-on-white, minimal composition — no title, no age, no "days" label.
- **Birthdate entry**: new **User** settings tab (5th category) with a
  "Birth date" action that opens the keyboard (format `YYYY-MM-DD`,
  validated; invalid input reopens the keyboard; empty clears).
- **Visibility**: the "Memento Mori" option appears in the on-device Sleep
  Screen picker only once a valid birthdate exists.
- **No date / no birthdate**: fall back silently to the default dark sleep
  screen (same pattern as COVER falling back with no book).

## Architecture

- `CrossPointSettings`: `MEMENTO_MORI = 7` in `SLEEP_SCREEN_MODE`;
  `char userBirthdate[11]` persisted via a `SettingInfo::String`
  (key `userBirthdate`, category `STR_CAT_USER` — web-editable; excluded
  from device lists because the device loop only surfaces non-STRING types
  per category). Helpers `hasValidBirthdate()` / `getBirthdate()` parse and
  range-check the string (leap-aware).
- **Load-order constraint**: `fromJson` clamps enum values against the
  shared list's option count *before* the birthdate string is loaded, so the
  Memento Mori label is *unconditionally* present in `getSettingsList()`
  (size 8). Hiding is device-side only: `rebuildSettingsLists()` pops the
  last sleep-screen option when no valid birthdate exists — guarded so an
  already-selected Memento Mori (e.g. set via web) is never trimmed into an
  out-of-bounds label lookup.
- `SettingsActivity`: `categoryCount` 4 → 5 (`User` tab), new
  `userSettings` list with a `SettingAction::SetBirthdate` action →
  `KeyboardEntryActivity` (maxLength 10). Result handler trims, validates,
  saves on change only, and rebuilds the lists so the sleep option appears
  immediately.
- `HalClock`: new `getDate(year, month, day, utcOffsetQuarterHoursBiased)`
  reading the RTC date with calendar-correct offset rollover, plus a public
  `static daysFromCivil()` (Howard Hinnant's civil-days algorithm) shared
  with the renderer. Returns false when the RTC is absent or unreliable
  (oscillator-stopped), which triggers the fallback.
- `SleepActivity::renderMementoMoriSleepScreen()`: computes days lived and
  full weeks since the last birthday (Feb-29 birthdays resolve via the civil
  arithmetic), draws the 13x4 grid centered, filled `fillRoundedRect` /
  empty `drawRoundedRect`, and the thousands-separated day count beneath.
  `HALF_REFRESH`, no inversion.

## Out of scope

Life-expectancy grids, age display, per-day granularity, multiple users.
