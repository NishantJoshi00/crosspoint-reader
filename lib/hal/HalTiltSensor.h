#pragma once

#include <Arduino.h>
#include <Imu.h>

// TODO: Move enums into new header and share with CrossPointSettings.h
namespace CrossPointOrientation {
enum Value : uint8_t { PORTRAIT = 0, LANDSCAPE_CW = 1, INVERTED = 2, LANDSCAPE_CCW = 3 };
}

namespace CrossPointTiltPageTurn {
enum Value : uint8_t { TILT_OFF = 0, TILT_NORMAL = 1, TILT_INVERTED = 2 };
}

class HalTiltSensor;
extern HalTiltSensor halTiltSensor;  // Singleton

class HalTiltSensor {
  bool _available = false;
  mutable Imu _sdkImu;

  // Tilt gesture state machine
  bool _tiltForwardEvent = false;  // Consumed by wasTiltedForward()
  bool _tiltBackEvent = false;     // Consumed by wasTiltedBack()
  bool _hadActivity = false;       // Non-consuming flag for sleep timer
  bool _inTilt = false;            // Currently tilted past threshold
  bool _isAwake = false;           // Tracks power state
  unsigned long _initMs = 0;       // Timestamp of sensor init
  unsigned long _lastTiltMs = 0;   // Debounce / cooldown
  unsigned long _wakeMs = 0;       // Timestamp of last wake() for stabilization

  // Shake gesture state machine (active only during a shake session)
  bool _shakeSession = false;      // update() defers to the session owner while set
  bool _shakeEvent = false;        // Consumed by wasShaken()
  bool _inShake = false;           // Currently above threshold; must return to neutral
  unsigned long _lastShakeMs = 0;  // Debounce / cooldown

  // Tuning constants
  static constexpr float RATE_THRESHOLD_DPS = 270.0f;      // Deg/sec speed to trigger flick
  static constexpr float NEUTRAL_RATE_DPS = 50.0f;         // Must stop moving below this rate before next trigger
  static constexpr unsigned long COOLDOWN_MS = 600;        // Minimum ms between triggers
  static constexpr unsigned long POLL_INTERVAL_MS = 50;    // 20 Hz polling
  static constexpr unsigned long WAKE_STABILIZE_MS = 300;  // Ignore readings after wake
  static constexpr float SHAKE_THRESHOLD_DPS = 450.0f;     // Total gyro magnitude to trigger a shake
  static constexpr float SHAKE_NEUTRAL_DPS = 80.0f;        // Must settle below this before next shake
  static constexpr unsigned long SHAKE_COOLDOWN_MS = 900;  // Minimum ms between shake triggers

  mutable unsigned long _lastPollMs = 0;

  bool readGyro(float& gx, float& gy, float& gz) const;

 public:
  // Call after BoardConfig has selected the active device.
  void begin();

  // Enables tilt polling state
  bool wake();

  // Puts tilt polling state to sleep
  bool deepSleep();

  // True if an IMU is present on this device
  bool isAvailable() const { return _available; }

  // Poll the accelerometer and update tilt gesture state.
  void update(const uint8_t mode, const uint8_t orientation, const bool inReader);

  // Returns true once per tilt-forward gesture (next page direction).
  // Consumed on read — subsequent calls return false until next gesture.
  bool wasTiltedForward();

  // Returns true once per tilt-back gesture (previous page direction).
  // Consumed on read.
  bool wasTiltedBack();

  // Non-consuming: true if any tilt activity occurred since last call.
  // Used to reset the auto-sleep inactivity timer.
  bool hadActivity();

  // Discard any pending tilt events (call when leaving reader or disabling tilt).
  void clearPendingEvents();

  // Shake session: an activity that wants shake gestures brackets its lifetime
  // with begin/end. While a session is active, update() (the global tilt
  // driver in main loop) leaves the IMU power state alone, so the session
  // owner's wake cannot be undone by a disabled tilt-page-turn setting.
  // Returns false when no IMU is present.
  bool beginShakeSession();
  void endShakeSession();

  // Poll the gyro for a shake gesture. Call every loop() during a session.
  void updateShake();

  // Returns true once per shake gesture. Consumed on read.
  bool wasShaken();
};
