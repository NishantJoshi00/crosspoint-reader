#pragma once
#include "activities/Activity.h"

// Boot splash: a bare white downward triangle (2px border) centered on the
// panel. Just before setup() hands off to the first real activity,
// playHandoffAnimation() rotates a black triangle out from underneath it,
// ending as a hexagram.
class BootActivity final : public Activity {
 public:
  explicit BootActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Boot", renderer, mappedInput) {}
  void onEnter() override;

  // Plays the phase-2 rotation (4 fast-refresh frames) over the framebuffer
  // the splash drew. Synchronous; call from setup() right before the first
  // activity replacement, only when the splash is on screen.
  static void playHandoffAnimation(const GfxRenderer& renderer);
};
