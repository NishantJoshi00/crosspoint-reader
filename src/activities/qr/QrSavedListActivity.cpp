#include "QrSavedListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <memory>

#include "MappedInputManager.h"
#include "QrCodeViewActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/QrStore.h"

void QrSavedListActivity::onEnter() {
  Activity::onEnter();

  // Rescan on every entry: the viewer below us or the web UI may have
  // changed the set of files.
  QrStore::listCodes(names);
  if (selectedIndex >= static_cast<int>(names.size())) selectedIndex = 0;
  requestUpdate();
}

void QrSavedListActivity::openSelected() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(names.size())) return;
  startActivityForResult(std::make_unique<QrCodeViewActivity>(renderer, mappedInput, names[selectedIndex]),
                         [](const ActivityResult&) {});
}

void QrSavedListActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelected();
    return;
  }

  const int itemCount = static_cast<int>(names.size());
  if (itemCount == 0) return;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  switch (handleListTouch(selectedIndex, itemCount, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      openSelected();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  const int pageItems = GUI.getListPageItems(contentHeight, false);
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, itemCount, pageItems);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, itemCount, pageItems);
    requestUpdate();
    return;
  }

  buttonNavigator.onNext([this, itemCount] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, itemCount] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, itemCount);
    requestUpdate();
  });
}

void QrSavedListActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_QR_SAVED));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  if (names.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_QR_NO_SAVED));
  } else {
    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(names.size()), selectedIndex,
                 [this](int index) { return names[index]; });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
