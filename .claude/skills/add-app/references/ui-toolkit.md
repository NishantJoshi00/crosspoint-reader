# UI Toolkit: Drawing, Lists, Text, Input

Everything renders through the `GUI` macro (UITheme). Themes own metrics;
never hardcode dimensions — `renderer.getScreenWidth()/getScreenHeight()` and
`GUI.getMetrics()` adapt to orientation and theme.

## Standard screen skeleton

Nearly every screen's `render(RenderLock&&)` is this shape:

```cpp
renderer.clearScreen();
const auto& metrics = UITheme::getInstance().getMetrics();
const auto pageWidth = renderer.getScreenWidth();
const auto pageHeight = renderer.getScreenHeight();

GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_MY_TITLE));

const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
// ... content into Rect{0, contentTop, pageWidth, contentHeight} ...

const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
renderer.displayBuffer();
```

`displayBuffer()` at the end is what pushes to the panel; forgetting it gives
a blank screen with no error. `mapLabels` reorders hint labels to match the
user's front-button remapping — pass hints through it, never position them
yourself.

## The list-screen trio

A selectable list is three cooperating pieces; copy an existing list activity
(`settings/OpdsServerListActivity` is the fullest example) rather than
composing from scratch:

1. **Render:** `GUI.drawList(renderer, rect, itemCount, selectedIndex,
   rowTitle, [rowSubtitle, rowIcon, rowValue, ...])` — trailing callbacks
   optional.
2. **Touch:** `Activity::handleListTouch(selectedIndex, itemCount, contentTop,
   contentHeight, hasSubtitle)` in `loop()`; returns
   `None/Consumed/Activated`. The `hasSubtitle` flag must match what you drew
   or hit-testing rows misalign.
3. **Buttons:** a `ButtonNavigator` member; each `loop()` call
   `buttonNavigator.onNext([&]{ selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount); requestUpdate(); })`
   and the `onPrevious` mirror. Page jumps: `nextPageIndex/previousPageIndex`
   with `GUI.getListPageItems(contentHeight, hasSubtitle)`, driven by
   vertical swipes.

For icon button menus (home-style) use `GUI.drawButtonMenu` with the `UIIcon`
enum; note the enum is theme-implemented — adding a new icon means touching
every theme, so prefer reusing an existing icon (see integration.md).

## Text drawing

- `drawText`/`drawCenteredText` take **y as the TOP of the glyph box** (the
  ascender is added internally). To stack lines, advance y by
  `renderer.getLineHeight(fontId)`. Do not guess pixel heights.
- Font IDs come from `src/fontIds.h` (`UI_10_FONT_ID`, `UI_12_FONT_ID`,
  `SMALL_FONT_ID` for chrome; Noto families for content).
- `tr(STR_X)` is a **token-pasting macro** (`StrId::` + your token). It only
  accepts a literal key. Holding a key in a variable? Type it as `StrId` and
  call `I18n::getInstance().get(id)` — `tr(variable)` fails to compile.
- Measuring: `renderer.getTextWidth(fontId, text)`.
- `std::string_view` never crosses a C-style boundary (`drawText`, paths,
  `snprintf`) — CLAUDE.md has the conversion patterns.

## Input idioms

- Activate on `wasReleased`, not `wasPressed`. Reason: the press that closed
  the previous activity can still deliver its release to yours. If your
  screen acts on a button that commonly closes the previous screen (Back
  especially), guard with a "press seen here first" bool — see
  `HomeActivity`'s `backPressSeen`.
- `wasSwipe()` is non-consuming; calling it multiple times per `loop()` is
  fine. Horizontal swipes conventionally mean prev/next (swipe left = next,
  matching reader page turns); vertical means list paging.
- Free-form taps: `wasScreenTapped(x, y)`, `wasTapInRect(...)`; row bands
  outside `drawList` geometry: `mappedInput.rowTouch(...)`.
- Page-turn semantics: `Button::PageBack` / `Button::PageForward` (side
  buttons, user-swappable). A viewer that flips between items should honor
  these plus `Left`/`Right` plus horizontal swipes.
