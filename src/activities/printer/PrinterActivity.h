#pragma once
#include <WiFiServer.h>

#include <memory>
#include <string>

#include "activities/Activity.h"
#include "network/ipp/HttpIppConnection.h"
#include "network/ipp/IppPrintService.h"
#include "network/ipp/PageScaler.h"

// Printer Mode: the X3 raises its own Wi-Fi AP, advertises itself over mDNS as
// an IPP/AirPrint printer, and renders whatever a computer prints to it on the
// e-ink panel. Confirm saves the shown page to /printouts as BMP.
//
// All protocol work lives in src/network/ipp (host-tested against real macOS
// print jobs); this activity provides the WiFi transport, screen, and
// lifecycle. Everything is allocated in onEnter and released in onExit; like
// CrossPointWebServerActivity we silent-restart on exit for a clean WiFi
// state.
class PrinterActivity final : public Activity {
 public:
  explicit PrinterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Printer", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return state == PrinterState::RUNNING || state == PrinterState::PAGE_SHOWING; }
  bool preventAutoSleep() override { return state != PrinterState::FAILED; }

 private:
  enum class PrinterState : uint8_t { STARTING, RUNNING, PAGE_SHOWING, FAILED };

  // ScaledPageSink bridging decoded pages into the activity.
  class Sink final : public ScaledPageSink {
    PrinterActivity& activity;

   public:
    explicit Sink(PrinterActivity& a) : activity(a) {}
    bool onScaledPageBegin(uint32_t pageIndex) override;
    void onScaledPageEnd(bool ok, uint32_t pageIndex) override;
  };

  PrinterState state = PrinterState::STARTING;
  std::string apSsid;
  std::string apIp;
  char printerUri[48] = {0};
  char moreInfoUrl[32] = {0};

  std::unique_ptr<uint8_t[]> pageBits;
  std::unique_ptr<Sink> sink;
  std::unique_ptr<IppPrintService> service;
  std::unique_ptr<HttpIppConnection> connection;
  WiFiServer server{631};
  bool serverStarted = false;

  int pagesReceived = 0;
  bool exitRequested = false;
  unsigned long savedBannerUntil = 0;

  bool startAccessPoint();
  void startMdns();
  void savePageToInbox();
  void renderWaitingScreen() const;
  void renderPage() const;
};
