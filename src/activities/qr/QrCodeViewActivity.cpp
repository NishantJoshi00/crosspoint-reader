#include "QrCodeViewActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "fontIds.h"
#include "util/QrStore.h"
#include "util/QrUtils.h"

void QrCodeViewActivity::onEnter() {
  Activity::onEnter();

  QrStore::listCodes(names);
  const auto it = std::find(names.begin(), names.end(), initialName);
  if (it != names.end()) {
    index = static_cast<int>(it - names.begin());
  } else if (!names.empty()) {
    index = 0;
  }
  loadCurrent();
}

const char* QrCodeViewActivity::qrSleepName() const {
  return (index >= 0 && index < static_cast<int>(names.size())) ? names[index].c_str() : nullptr;
}

void QrCodeViewActivity::loadCurrent() {
  payload.clear();
  if (index >= 0 && index < static_cast<int>(names.size())) {
    QrStore::loadPayload(names[index], payload);
  }
  requestUpdate();
}

void QrCodeViewActivity::step(int direction) {
  const int count = static_cast<int>(names.size());
  if (count < 2) return;
  index = (index + direction + count) % count;
  loadCurrent();
}

void QrCodeViewActivity::loop() {
  int x = 0;
  int y = 0;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
      mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasSwipe() == MappedInputManager::SwipeDir::Right) {
    step(-1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::PageForward) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasSwipe() == MappedInputManager::SwipeDir::Left) {
    step(1);
    return;
  }
}

void QrCodeViewActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (payload.empty()) {
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2, tr(STR_QR_NO_SAVED));
    renderer.displayBuffer();
    return;
  }

  // The bottom strip holds the code's name (and position when there are
  // several codes); the QR gets the rest of the screen.
  const int nameLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int positionLineHeight = names.size() > 1 ? renderer.getLineHeight(SMALL_FONT_ID) : 0;
  const int stripHeight = nameLineHeight + positionLineHeight + 10;
  const Rect qrBounds(20, 20, pageWidth - 40, pageHeight - stripHeight - 30);
  QrUtils::drawQrCode(renderer, qrBounds, payload);

  const int textTop = pageHeight - stripHeight;
  renderer.drawCenteredText(UI_12_FONT_ID, textTop, names[index].c_str());
  if (names.size() > 1) {
    char position[16];
    snprintf(position, sizeof(position), "%d / %d", index + 1, static_cast<int>(names.size()));
    renderer.drawCenteredText(SMALL_FONT_ID, textTop + nameLineHeight, position);
  }

  renderer.displayBuffer();
}
