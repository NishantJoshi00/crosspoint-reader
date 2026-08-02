#include "RngActivity.h"

#include <GfxRenderer.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <esp_random.h>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int PICKER_ITEM_COUNT = 2;  // Coin, Dice

// Pip layout per die value: 3x3 grid, bit index = row * 3 + col.
constexpr uint16_t PIP_MASKS[7] = {0, 0x010, 0x101, 0x111, 0x145, 0x155, 0x16D};

constexpr unsigned long SCRAMBLE_FRAME_MS = 150;
}  // namespace

void RngActivity::onEnter() {
  Activity::onEnter();
  shakeAvailable = halTiltSensor.beginShakeSession();
  requestUpdate();
}

void RngActivity::onExit() {
  halTiltSensor.endShakeSession();
  Activity::onExit();
}

void RngActivity::rollValue(const int index) {
  const uint32_t r = esp_random();
  values[index] = mode == Mode::COIN ? static_cast<uint8_t>(r % 2) : static_cast<uint8_t>(r % 6 + 1);
}

void RngActivity::rollAll() {
  for (int i = 0; i < itemCount; i++) {
    rollValue(i);
  }
}

void RngActivity::enterRollScreen() {
  mode = pickerIndex == 0 ? Mode::COIN : Mode::DICE;
  screen = Screen::ROLL;
  itemCount = 1;
  rollAll();
  requestUpdate();
}

void RngActivity::loop() {
  if (screen == Screen::PICKER) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      activityManager.goHome(HomeMenuItem::RNG);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      enterRollScreen();
      return;
    }

    const auto& metrics = UITheme::getInstance().getMetrics();
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight =
        renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
    switch (handleListTouch(pickerIndex, PICKER_ITEM_COUNT, contentTop, contentHeight, false)) {
      case ListTouchResult::Activated:
        enterRollScreen();
        return;
      case ListTouchResult::Consumed:
        return;
      case ListTouchResult::None:
        break;
    }

    buttonNavigator.onNext([this] {
      pickerIndex = ButtonNavigator::nextIndex(pickerIndex, PICKER_ITEM_COUNT);
      requestUpdate();
    });
    buttonNavigator.onPrevious([this] {
      pickerIndex = ButtonNavigator::previousIndex(pickerIndex, PICKER_ITEM_COUNT);
      requestUpdate();
    });
    return;
  }

  // ROLL screen
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    screen = Screen::PICKER;
    requestUpdate();
    return;
  }

  // Side buttons adjust the item count within [1, 6]; a new slot gets its own
  // roll, existing faces keep their values.
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (itemCount < MAX_ITEMS) {
      rollValue(itemCount);
      itemCount++;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (itemCount > MIN_ITEMS) {
      itemCount--;
      requestUpdate();
    }
    return;
  }

  if (shakeAvailable) {
    halTiltSensor.updateShake();
    if (halTiltSensor.wasShaken()) {
      rollAll();
      scramblePending = true;
      requestUpdate();
    }
  }
}

void RngActivity::drawDie(const int x, const int y, const int size, const uint8_t value) const {
  const int corner = size / 8;
  renderer.drawRoundedRect(x, y, size, size, 3, corner, true);

  const int pipRadius = size / 12;
  const int inset = size / 4;
  const int step = (size - inset * 2) / 2;
  const uint16_t mask = PIP_MASKS[value <= 6 ? value : 0];
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) {
      if ((mask >> (row * 3 + col)) & 1) {
        const int cx = x + inset + col * step;
        const int cy = y + inset + row * step;
        renderer.fillRoundedRect(cx - pipRadius, cy - pipRadius, pipRadius * 2, pipRadius * 2, pipRadius, Color::Black);
      }
    }
  }
}

void RngActivity::drawCoin(const int x, const int y, const int size, const uint8_t value) const {
  renderer.drawRoundedRect(x, y, size, size, 3, size / 2, true);
  // Inner ring for a coin look
  const int ringInset = size / 10;
  renderer.drawRoundedRect(x + ringInset, y + ringInset, size - ringInset * 2, size - ringInset * 2, 1,
                           (size - ringInset * 2) / 2, true);

  const char* face = value == 0 ? tr(STR_RNG_HEADS_ABBR) : tr(STR_RNG_TAILS_ABBR);
  const int textWidth = renderer.getTextWidth(NOTOSANS_18_FONT_ID, face, EpdFontFamily::BOLD);
  const int lineHeight = renderer.getLineHeight(NOTOSANS_18_FONT_ID);
  renderer.drawText(NOTOSANS_18_FONT_ID, x + (size - textWidth) / 2, y + (size - lineHeight) / 2, face, true,
                    EpdFontFamily::BOLD);
}

void RngActivity::drawFaces(const bool scramble) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  const int cols = itemCount >= 3 ? 3 : itemCount;
  const int rows = (itemCount + cols - 1) / cols;
  const int gap = 24;
  const int maxFaceWidth = (pageWidth - gap * (cols + 1)) / cols;
  const int maxFaceHeight = (contentHeight - gap * (rows + 1)) / rows;
  int faceSize = maxFaceWidth < maxFaceHeight ? maxFaceWidth : maxFaceHeight;
  if (faceSize > 160) faceSize = 160;

  const int gridHeight = rows * faceSize + (rows - 1) * gap;
  const int startY = contentTop + (contentHeight - gridHeight) / 2;

  for (int i = 0; i < itemCount; i++) {
    const int row = i / cols;
    const int colsInRow = row == rows - 1 ? itemCount - row * cols : cols;
    const int col = i % cols;
    const int rowWidth = colsInRow * faceSize + (colsInRow - 1) * gap;
    const int startX = (pageWidth - rowWidth) / 2;
    const int x = startX + col * (faceSize + gap);
    const int y = startY + row * (faceSize + gap);

    uint8_t value = values[i];
    if (scramble) {
      const uint32_t r = esp_random();
      value = mode == Mode::COIN ? static_cast<uint8_t>(r % 2) : static_cast<uint8_t>(r % 6 + 1);
    }
    if (mode == Mode::COIN) {
      drawCoin(x, y, faceSize, value);
    } else {
      drawDie(x, y, faceSize, value);
    }
  }
}

void RngActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  if (screen == Screen::PICKER) {
    renderer.clearScreen();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_RNG_APP));

    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, PICKER_ITEM_COUNT, pickerIndex,
                 [](int index) {
                   return std::string(I18n::getInstance().get(index == 0 ? StrId::STR_RNG_COIN : StrId::STR_RNG_DICE));
                 });

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // ROLL screen
  const char* title = mode == Mode::COIN ? tr(STR_RNG_COIN) : tr(STR_RNG_DICE);

  if (!shakeAvailable) {
    renderer.clearScreen();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2, tr(STR_RNG_NO_IMU), true);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // Scramble frame: throwaway random faces, briefly shown before the result.
  if (scramblePending) {
    renderer.clearScreen();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);
    drawFaces(true);
    renderer.displayBuffer();
    vTaskDelay(pdMS_TO_TICKS(SCRAMBLE_FRAME_MS));
    scramblePending = false;
  }

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);
  drawFaces(false);
  renderer.drawCenteredText(
      SMALL_FONT_ID,
      pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - renderer.getLineHeight(SMALL_FONT_ID),
      tr(STR_RNG_SHAKE_HINT), true);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
