#pragma once
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Random number generator app: pick Coin or Dice, adjust the item count with
// the side buttons (1-6), shake the device to roll. Shake gestures come from
// HalTiltSensor's shake session; boards without an IMU get a notice screen.
class RngActivity final : public Activity {
  enum class Screen : uint8_t { PICKER, ROLL };
  enum class Mode : uint8_t { COIN, DICE };

  static constexpr int MAX_ITEMS = 6;
  static constexpr int MIN_ITEMS = 1;

  ButtonNavigator buttonNavigator;
  Screen screen = Screen::PICKER;
  Mode mode = Mode::COIN;
  int pickerIndex = 0;
  int itemCount = 1;
  uint8_t values[MAX_ITEMS] = {0};  // Coin: 0/1 (heads/tails). Dice: 1-6.
  bool scramblePending = false;     // Render a throwaway scramble frame before the result
  bool shakeAvailable = false;

  void rollValue(int index);
  void rollAll();
  void enterRollScreen();
  void drawFaces(bool scramble) const;
  void drawDie(int x, int y, int size, uint8_t value) const;
  void drawCoin(int x, int y, int size, uint8_t value) const;

 public:
  explicit RngActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Rng", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
