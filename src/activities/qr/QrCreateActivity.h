#pragma once

#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Guided QR creation: pick a payload type, fill its fields on the keyboard,
// name the code, save it to /qrcodes and open it in the viewer.
class QrCreateActivity final : public Activity {
 public:
  explicit QrCreateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("QrCreate", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class QrType : uint8_t { Url, Wifi, Phone, Email, Sms, Count };
  enum class WifiSecurity : uint8_t { Wpa, Wep, Open, Count };
  // Which list this screen currently shows; keyboard fields run as
  // subactivities on top and return here through result handlers.
  enum class Mode : uint8_t { TypeSelect, SecuritySelect };

  ButtonNavigator buttonNavigator;
  Mode mode = Mode::TypeSelect;
  int selectedIndex = 0;
  bool saveFailed = false;

  QrType type = QrType::Url;
  WifiSecurity security = WifiSecurity::Wpa;
  std::string primary;    // URL, SSID, phone number, email address or SMS number
  std::string secondary;  // WiFi password or SMS message

  void beginType(QrType selected);
  void promptSecondary();
  void promptName();
  void saveAndShow(const std::string& rawName);
  std::string buildPayload() const;

  void handleListSelection();
  int listItemCount() const;
};
