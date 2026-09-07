#pragma once

#include <cstdint>

// Only user input and completed pages renew the session, never discovery polls.
class PrinterIdleTimer {
  static constexpr uint32_t DEFAULT_TIMEOUT_MS = 10 * 60 * 1000;
  uint32_t timeoutMs = DEFAULT_TIMEOUT_MS;
  uint32_t renewedAt = 0;

 public:
  void start(uint32_t now, uint32_t timeout) {
    // Reading's "Never" setting must not leave the printer radio on forever.
    timeoutMs = timeout != 0 ? timeout : DEFAULT_TIMEOUT_MS;
    renew(now);
  }
  void renew(uint32_t now) { renewedAt = now; }
  bool expired(uint32_t now) const { return now - renewedAt >= timeoutMs; }
  uint32_t remainingMinutes(uint32_t now) const {
    if (expired(now)) return 0;
    return (timeoutMs - (now - renewedAt) + 59999) / 60000;
  }
};
