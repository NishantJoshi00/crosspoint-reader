#include "QrCreateActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <memory>

#include "MappedInputManager.h"
#include "QrCodeViewActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/QrStore.h"

namespace {

std::string trimmed(const std::string& value) {
  size_t start = 0;
  size_t end = value.size();
  while (start < end && (value[start] == ' ' || value[start] == '\t')) start++;
  while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t')) end--;
  return value.substr(start, end - start);
}

// The WIFI: format reserves \ ; , : " — escape them with a backslash.
std::string escapeWifiField(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size() + 4);
  for (const char c : value) {
    if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') escaped += '\\';
    escaped += c;
  }
  return escaped;
}

StrId typeLabel(int index) {
  switch (index) {
    case 0:
      return StrId::STR_QR_TYPE_URL;
    case 1:
      return StrId::STR_QR_TYPE_WIFI;
    case 2:
      return StrId::STR_QR_TYPE_PHONE;
    case 3:
      return StrId::STR_QR_TYPE_EMAIL;
    default:
      return StrId::STR_QR_TYPE_SMS;
  }
}

StrId securityLabel(int index) {
  switch (index) {
    case 0:
      return StrId::STR_QR_SECURITY_WPA;
    case 1:
      return StrId::STR_QR_SECURITY_WEP;
    default:
      return StrId::STR_QR_SECURITY_NONE;
  }
}

}  // namespace

void QrCreateActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

int QrCreateActivity::listItemCount() const {
  return mode == Mode::TypeSelect ? static_cast<int>(QrType::Count) : static_cast<int>(WifiSecurity::Count);
}

void QrCreateActivity::beginType(QrType selected) {
  type = selected;
  primary.clear();
  secondary.clear();
  security = WifiSecurity::Wpa;
  saveFailed = false;

  StrId title = StrId::STR_QR_ENTER_URL;
  std::string prefill;
  InputType inputType = InputType::Text;
  size_t maxLength = 64;

  switch (type) {
    case QrType::Url:
      title = StrId::STR_QR_ENTER_URL;
      prefill = "https://";
      inputType = InputType::Url;
      maxLength = 512;
      break;
    case QrType::Wifi:
      title = StrId::STR_QR_ENTER_SSID;
      maxLength = 32;
      break;
    case QrType::Phone:
    case QrType::Sms:
      title = StrId::STR_QR_ENTER_PHONE;
      maxLength = 24;
      break;
    case QrType::Email:
      title = StrId::STR_QR_ENTER_EMAIL;
      maxLength = 64;
      break;
    default:
      return;
  }

  auto handler = [this](const ActivityResult& result) {
    if (result.isCancelled) {
      requestUpdate();
      return;
    }
    primary = trimmed(std::get<KeyboardResult>(result.data).text);
    if (primary.empty() || (type == QrType::Url && primary == "https://")) {
      requestUpdate();
      return;
    }
    if (type == QrType::Wifi) {
      // Security first so an open network can skip the password field.
      mode = Mode::SecuritySelect;
      selectedIndex = 0;
      requestUpdate();
    } else if (type == QrType::Sms) {
      promptSecondary();
    } else {
      promptName();
    }
  };
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput,
                                                                 I18n::getInstance().get(title), prefill, maxLength,
                                                                 inputType),
                         handler);
}

void QrCreateActivity::promptSecondary() {
  const bool isWifi = type == QrType::Wifi;
  auto handler = [this](const ActivityResult& result) {
    if (result.isCancelled) {
      mode = Mode::TypeSelect;
      requestUpdate();
      return;
    }
    secondary = std::get<KeyboardResult>(result.data).text;
    promptName();
  };
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(
                             renderer, mappedInput, isWifi ? tr(STR_QR_ENTER_PASSWORD) : tr(STR_QR_ENTER_MESSAGE), "",
                             isWifi ? 63 : 160, InputType::Text),
                         handler);
}

void QrCreateActivity::promptName() {
  // Suggest a name from the content; the URL prefix is rarely a good name.
  const std::string suggestion = type == QrType::Url ? "" : primary;
  auto handler = [this](const ActivityResult& result) {
    if (result.isCancelled) {
      mode = Mode::TypeSelect;
      requestUpdate();
      return;
    }
    saveAndShow(std::get<KeyboardResult>(result.data).text);
  };
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_QR_ENTER_NAME),
                                                                 suggestion, QrStore::MAX_NAME_LENGTH, InputType::Text),
                         handler);
}

void QrCreateActivity::saveAndShow(const std::string& rawName) {
  const std::string name = QrStore::sanitizeName(rawName);
  if (name.empty()) {
    promptName();
    return;
  }

  const std::string finalName = QrStore::uniqueName(name);
  mode = Mode::TypeSelect;
  if (!QrStore::savePayload(finalName, buildPayload())) {
    saveFailed = true;
    requestUpdate();
    return;
  }

  // Show the new code; when the viewer closes, drop back to the QR menu.
  startActivityForResult(std::make_unique<QrCodeViewActivity>(renderer, mappedInput, finalName),
                         [this](const ActivityResult&) { finish(); });
}

std::string QrCreateActivity::buildPayload() const {
  switch (type) {
    case QrType::Url:
      return primary;
    case QrType::Wifi: {
      std::string payload = "WIFI:T:";
      payload += security == WifiSecurity::Wpa ? "WPA" : (security == WifiSecurity::Wep ? "WEP" : "nopass");
      payload += ";S:";
      payload += escapeWifiField(primary);
      if (security != WifiSecurity::Open) {
        payload += ";P:";
        payload += escapeWifiField(secondary);
      }
      payload += ";;";
      return payload;
    }
    case QrType::Phone:
      return "tel:" + primary;
    case QrType::Email:
      return "mailto:" + primary;
    case QrType::Sms:
      return "SMSTO:" + primary + ":" + secondary;
    default:
      return "";
  }
}

void QrCreateActivity::handleListSelection() {
  if (mode == Mode::TypeSelect) {
    beginType(static_cast<QrType>(selectedIndex));
    return;
  }

  security = static_cast<WifiSecurity>(selectedIndex);
  if (security == WifiSecurity::Open) {
    promptName();
  } else {
    promptSecondary();
  }
}

void QrCreateActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (mode == Mode::SecuritySelect) {
      mode = Mode::TypeSelect;
      selectedIndex = 0;
      requestUpdate();
    } else {
      finish();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    handleListSelection();
    return;
  }

  const int itemCount = listItemCount();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  switch (handleListTouch(selectedIndex, itemCount, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      handleListSelection();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
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

void QrCreateActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const bool typeSelect = mode == Mode::TypeSelect;
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 typeSelect ? tr(STR_QR_CREATE) : tr(STR_QR_SECURITY));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, listItemCount(), selectedIndex,
               [typeSelect](int index) {
                 return std::string(I18n::getInstance().get(typeSelect ? typeLabel(index) : securityLabel(index)));
               });

  if (saveFailed) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing,
                              tr(STR_QR_SAVE_FAILED));
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
