#include <HalClock.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include <cassert>
#include <cstdio>

#include "activities/printer/PrinterIdleTimer.h"

namespace {
uint32_t ticks = 0;
bool rtcPresent = true;
bool rtcReadable = true;
bool rtcWriteWorks = true;
int readsToFail = 0;
int reads = 0;
int ntpStarts = 0;
bool ntpRunning = false;
bool ntpResponds = false;
sntp_sync_status_t ntpStatus = SNTP_SYNC_STATUS_RESET;
Rtc::DateTime clockDate{2026, 9, 7, 12, 30, 0, 1};
}  // namespace

TestWifi WiFi;
unsigned long millis() { return ticks; }
void delay(unsigned long ms) { ticks += ms; }
void configTzTime(const char*, const char*, const char*) {
  ntpRunning = true;
  ++ntpStarts;
  if (ntpResponds) ntpStatus = SNTP_SYNC_STATUS_COMPLETED;
}
bool esp_sntp_enabled() { return ntpRunning; }
void esp_sntp_stop() { ntpRunning = false; }
void sntp_set_sync_status(sntp_sync_status_t status) { ntpStatus = status; }
sntp_sync_status_t sntp_get_sync_status() { return ntpStatus; }

bool Rtc::begin() { return rtcPresent; }
bool Rtc::now(DateTime& dt) {
  ++reads;
  if (readsToFail > 0) {
    --readsToFail;
    return false;
  }
  if (!rtcReadable) return false;
  dt = clockDate;
  return true;
}
bool Rtc::set(const DateTime& dt) {
  if (!rtcWriteWorks) return false;
  clockDate = dt;
  return true;
}

int main() {
  HalClock clock;
  clock.begin();
  uint16_t y;
  uint8_t m, d, h, minute;

  // A valid hardware clock works offline, including much later in the session.
  assert(clock.getDate(y, m, d) && y == 2026 && m == 9 && d == 7);
  ticks += 20UL * 24 * 60 * 60 * 1000;
  clockDate.day = 27;
  assert(clock.getDate(y, m, d) && d == 27);
  assert(ntpStarts == 0);

  // Recover a transient bus read without an internet connection.
  readsToFail = 1;
  const int readsBefore = reads;
  assert(clock.hasValidTime() && reads == readsBefore + 2);

  // Persistent loss must not reuse a stale time, even just after a cached read.
  assert(clock.getTime(h, minute));
  rtcReadable = false;
  assert(!clock.hasValidTime());
  assert(!clock.getTime(h, minute));
  assert(!clock.getDate(y, m, d));
  rtcReadable = true;

  // Validate dates before civil arithmetic can normalize malformed registers.
  clockDate = {2026, 2, 29, 0, 0, 0, 0};
  assert(!clock.hasValidTime());
  clockDate.year = 2024;
  assert(clock.hasValidTime());
  clockDate.month = 13;
  assert(!clock.hasValidTime());
  clockDate.month = 2;
  clockDate.hour = 24;
  assert(!clock.hasValidTime());

  // Both offset extremes cross date/year boundaries correctly.
  clockDate = {2026, 1, 1, 0, 15, 0, 0};
  assert(clock.getDate(y, m, d, 0) && y == 2025 && m == 12 && d == 31);
  clockDate = {2024, 2, 29, 23, 30, 0, 0};
  assert(clock.getDate(y, m, d, 104) && y == 2024 && m == 3 && d == 1);

  // A missing clock does no NTP work; failed syncs cannot become successful
  // merely because a prior SNTP session reported completion.
  rtcPresent = false;
  HalClock absent;
  absent.begin();
  assert(!absent.isAvailable() && !absent.getDate(y, m, d));
  assert(!absent.syncFromNTP() && ntpStarts == 0);
  rtcPresent = true;
  assert(!clock.syncFromNTP() && ntpStarts == 0);
  WiFi.connectionStatus = WL_CONNECTED;
  ntpRunning = true;
  ntpStatus = SNTP_SYNC_STATUS_COMPLETED;
  const auto beforeTimeout = ticks;
  assert(!clock.syncFromNTP());
  assert(ticks - beforeTimeout == 5000 && !ntpRunning);
  ntpResponds = true;
  assert(clock.syncFromNTP() && !ntpRunning && clock.hasValidTime());
  rtcReadable = false;  // Successful write, but oscillator still reports invalid.
  assert(!clock.syncFromNTP() && !ntpRunning);
  rtcReadable = true;
  rtcWriteWorks = false;
  assert(!clock.syncFromNTP() && !ntpRunning);

  // Idle expiry, explicit reset and print-completion reset use the same timer.
  PrinterIdleTimer timer;
  timer.start(100, 600000);
  assert(timer.remainingMinutes(100) == 10);
  assert(timer.remainingMinutes(60100) == 9);
  assert(!timer.expired(600099) && timer.remainingMinutes(600099) == 1);
  assert(timer.expired(600100) && timer.remainingMinutes(600100) == 0);
  timer.renew(600099);
  assert(!timer.expired(600100) && timer.remainingMinutes(600100) == 10);
  timer.renew(700000);
  assert(!timer.expired(1299999) && timer.expired(1300000));

  // Read-only discovery/countdown checks cannot extend the battery budget.
  timer.start(0, 60000);
  for (uint32_t now = 0; now < 60000; now += 10) assert(timer.remainingMinutes(now) == 1);
  assert(timer.expired(60000));

  // The millisecond clock wraps approximately every 50 days.
  timer.start(UINT32_MAX - 29999, 60000);
  assert(!timer.expired(29999));
  assert(timer.expired(30000));
  timer.start(0, 0);  // "Never" for reading must not disable the printer budget.
  assert(timer.remainingMinutes(0) == 10);
  assert(!timer.expired(599999) && timer.expired(600000));
  std::puts("PASS: offline clock, recovery, invalid dates, timezone rollover, bounded NTP, printer expiry and reset");
}
