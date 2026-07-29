#include "QrMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <memory>

#include "MappedInputManager.h"
#include "QrCreateActivity.h"
#include "QrSavedListActivity.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"

namespace {
constexpr int MENU_ITEM_COUNT = 2;  // Create, Saved
}

void QrMenuActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void QrMenuActivity::openSelected() {
  auto refresh = [this](const ActivityResult&) { requestUpdate(); };
  if (selectedIndex == 0) {
    startActivityForResult(std::make_unique<QrCreateActivity>(renderer, mappedInput), refresh);
  } else {
    startActivityForResult(std::make_unique<QrSavedListActivity>(renderer, mappedInput), refresh);
  }
}

void QrMenuActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goHome(HomeMenuItem::QR_CODES);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelected();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  switch (handleListTouch(selectedIndex, MENU_ITEM_COUNT, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      openSelected();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, MENU_ITEM_COUNT);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, MENU_ITEM_COUNT);
    requestUpdate();
  });
}

void QrMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_QR_CODES));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, MENU_ITEM_COUNT, selectedIndex, [](int index) {
    return std::string(I18n::getInstance().get(index == 0 ? StrId::STR_QR_CREATE : StrId::STR_QR_SAVED));
  });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
