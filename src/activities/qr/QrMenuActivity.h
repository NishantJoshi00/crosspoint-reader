#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Entry point for the QR codes feature: Create a new code or browse saved ones.
class QrMenuActivity final : public Activity {
 public:
  explicit QrMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("QrMenu", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  void openSelected();
};
