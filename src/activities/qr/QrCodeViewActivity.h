#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"

// Fullscreen viewer for a saved QR code: the code centered with its name
// below, PageBack/PageForward (and Left/Right or swipes) cycling through
// the other saved codes. Reports its current name via qrSleepName() so a
// deep sleep keeps the code on the panel and wake resumes into it.
class QrCodeViewActivity final : public Activity {
 public:
  explicit QrCodeViewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialName)
      : Activity("QrCodeView", renderer, mappedInput), initialName(std::move(initialName)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  const char* qrSleepName() const override;

 private:
  std::string initialName;
  std::vector<std::string> names;
  int index = -1;
  std::string payload;

  void loadCurrent();
  void step(int direction);
};
