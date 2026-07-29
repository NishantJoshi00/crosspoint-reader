# Activity Model: Lifecycle, Navigation, Results

Read `src/activities/Activity.h` and `src/activities/ActivityManager.h` first;
this file explains the contract and the traps that are not obvious from the
headers.

## Lifecycle

Activities are heap-allocated and deleted on exit. `onEnter()` and `onExit()`
are your resource-ownership boundary: everything allocated in `onEnter()`
(buffers, FreeRTOS tasks, member `HalFile`s) is freed/deleted/closed in
`onExit()`, in reverse order, tasks first. `loop()` runs every main-loop tick.

`render(RenderLock&&)` runs on a **separate render task**, not the main loop.
Consequences:

- Never draw from `loop()`; mutate state, then call `requestUpdate()`.
  `requestUpdate(true)` renders immediately; the default defers to the end of
  the loop iteration (coalesces multiple requests).
- `requestUpdateAndWait()` blocks until the frame is on screen — required when
  the next thing you do depends on the user having seen it. Never call it from
  the render task or while holding a `RenderLock`.
- A typical screen's `onEnter()` is just setup + `requestUpdate()`.

Optional virtuals worth knowing: `skipLoopDelay()` (tight-loop activities),
`preventAutoSleep()` (activity must survive idle timeout, e.g. servers),
`isHomeActivity()` / `isReaderActivity()` (identity checks used by manager
logic), `handleHomeGesture()` (veto the global home gesture), `qrSleepName()`
(see integration.md sleep section for the pattern it exemplifies).

## Navigation

`ActivityManager` (singleton `activityManager`) owns a current activity plus a
stack. Three ways to move:

- `replaceActivity(...)` — destroys current activity AND clears the stack.
  This is what the `goTo*()` wrappers use. Top-level screens (home menu
  destinations) are replacements.
- `pushActivity(...)` / `startActivityForResult(activity, handler)` — current
  activity is suspended on the stack; the child's `finish()` pops back.
- `finish()` — pop self. Popping the last activity falls through to
  `goHome()`, so an activity entered via `replaceActivity` can simply
  `finish()` and land on home — but if home should highlight your menu entry,
  call `activityManager.goHome(HomeMenuItem::YOURS)` instead.

Transitions are **deferred**: they set a pending action processed on the next
manager loop iteration. Do not expect the new activity to exist immediately
after the call; just `return` from `loop()` after requesting navigation.

Add a `goToYourApp()` wrapper on `ActivityManager` for your entry point (see
`goToFileTransfer()` for the two-line shape) rather than making callers build
`make_unique` themselves.

## Results and wizard chaining

`ActivityResult` is a `std::variant` (`ActivityResult.h`). To return data,
reuse an existing result struct or add one to the variant. The child calls
`setResult(...)` then `finish()`; cancellation is `result.isCancelled`.

**Result handlers may launch the next child.** The manager moves the handler
out before invoking it precisely so a handler can call
`startActivityForResult` again. This is the idiomatic multi-step wizard: each
field's handler stores the value and launches the next prompt. No state
machine needed for the linear parts; keep an explicit `Mode` enum only for the
screens the parent itself renders between prompts.

## Text entry

`KeyboardEntryActivity(renderer, mappedInput, title, initialText, maxLength,
inputType)` with `InputType::{Text, Password, Url}` (`Url` adds a snippet
panel with common URL fragments). Returns `KeyboardResult{text}`; Back sets
`isCancelled`. Handler skeleton:

```cpp
auto handler = [this](const ActivityResult& result) {
  if (result.isCancelled) { requestUpdate(); return; }
  value = std::get<KeyboardResult>(result.data).text;
  promptNextFieldOrFinish();
};
startActivityForResult(
    std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_MY_TITLE), prefill, maxLen, InputType::Text),
    handler);
```

Trim user input yourself; the keyboard does not.

## Nested flows

`ActivityWithSubactivity` exists for hosting a child inside a parent screen
(see `docs/activity-manager.md`). Prefer plain push/pop unless you truly need
both on screen.
