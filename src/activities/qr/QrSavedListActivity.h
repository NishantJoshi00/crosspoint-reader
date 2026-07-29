#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// List of saved QR codes; selecting one opens the fullscreen viewer.
class QrSavedListActivity final : public Activity {
 public:
  explicit QrSavedListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("QrSavedList", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  std::vector<std::string> names;
  int selectedIndex = 0;

  void openSelected();
};
