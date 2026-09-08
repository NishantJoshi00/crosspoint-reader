#pragma once
#include <WiFi.h>

#include <memory>
#include <string>
#include <vector>

#include "PrinterIdleTimer.h"
#include "activities/Activity.h"
#include "network/ipp/HttpIppConnection.h"
#include "network/ipp/IppPrintService.h"
#include "network/ipp/PageScaler.h"
#include "util/ButtonNavigator.h"

// Printer Mode: the X3 ("penguin") becomes an IPP/AirPrint printer. Join an
// existing network (STA — computer keeps internet) or raise the
// "literate-penguin" hotspot (portable), then anything printed to "penguin"
// renders on the e-ink panel and saves automatically to /printouts. Confirm
// resets the visible idle timer; Left aborts an in-flight job; Back exits.
//
// Protocol core in src/network/ipp, host-tested against real macOS jobs.
// Services allocate at session start and free in onExit; silent-restart on exit
// after WiFi use (CrossPointWebServerActivity convention).
class PrinterActivity final : public Activity {
 public:
  explicit PrinterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Printer", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // The normal 10 ms loop delay lets the CPU idle while the listener stays live.
  bool preventAutoSleep() override { return isSessionActive(); }
  bool wantsAutoSleep() const override { return isSessionActive() && idleTimer.expired(millis()); }
  bool prepareSleepScreen(bool fromTimeout) override;

 private:
  enum class PrinterState : uint8_t { MODE_SELECT, WIFI_SELECTING, STARTING, RUNNING, PAGE_SHOWING, FAILED };

  // Draws decoded rows straight into the panel framebuffer — no page-sized
  // staging buffer (48 KB the device does not have with WiFi up).
  class Sink final : public ScaledPageSink {
    PrinterActivity& activity;

   public:
    explicit Sink(PrinterActivity& a) : activity(a) {}
    bool onScaledPageBegin(uint32_t pageIndex, int boxX, int boxY, int boxW, int boxH) override;
    bool onScaledRow(int y, int xOffset, const uint8_t* rowBits, int width) override;
    void onScaledPageEnd(bool ok, uint32_t pageIndex) override;
  };

  PrinterState state = PrinterState::MODE_SELECT;
  ButtonNavigator buttonNavigator;
  int modeIndex = 0;
  bool isApMode = false;
  PrinterIdleTimer idleTimer;
  uint32_t shownMinutes = 0;
  bool networkReady = false;
  bool discoveryReady = false;
  unsigned long lastNetworkCheck = 0;
  wifi_ps_type_t previousWifiSleep = WIFI_PS_MIN_MODEM;
  bool wifiSleepChanged = false;

  std::string netSsid;
  std::string netIp;
  char printerUri[48] = {0};
  char moreInfoUrl[32] = {0};

  std::unique_ptr<Sink> sink;
  std::unique_ptr<IppPrintService> service;
  std::unique_ptr<HttpIppConnection> connection;
  WiFiServer server{631};
  bool serverStarted = false;

  // The printout queue lives on the SD card (/printouts): pages are far too
  // big to keep in RAM, so this holds filenames only and each page is loaded
  // on demand when the user traverses to it.
  std::vector<std::string> queue;
  int queueIndex = -1;

  void beginModeSelect();
  void onModeChosen(bool hotspot);
  bool startAccessPoint();
  void startServices();
  void failStart();
  void updateNetwork();
  void updateAddress();
  void startMdns();
  bool isSessionActive() const { return state == PrinterState::RUNNING || state == PrinterState::PAGE_SHOWING; }
  void renewTimeout();
  void drawTimeout() const;
  void clearJob();  // abort an in-flight job (Left held during transfer)
  void loadQueue();
  void savePageToQueue();
  void showQueueEntry(int index);
  void drawPageHints() const;
  void renderModeSelect() const;
  void renderWaitingScreen() const;
};
