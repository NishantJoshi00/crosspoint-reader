#pragma once
#include <WiFiServer.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "network/ipp/HttpIppConnection.h"
#include "network/ipp/IppPrintService.h"
#include "network/ipp/PageScaler.h"
#include "util/ButtonNavigator.h"

// Printer Mode: the X3 ("penguin") becomes an IPP/AirPrint printer. Join an
// existing network (STA — computer keeps internet) or raise the
// "literate-penguin" hotspot (portable), then anything printed to "penguin"
// renders on the e-ink panel with a paper-feed reveal effect. Confirm saves
// the page to /printouts as BMP; Left aborts/clears any in-flight job; Back
// exits.
//
// Protocol core in src/network/ipp, host-tested against real macOS jobs.
// Everything allocates in onEnter and frees in onExit; silent-restart on exit
// after WiFi use (CrossPointWebServerActivity convention).
class PrinterActivity final : public Activity {
 public:
  explicit PrinterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Printer", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return state == PrinterState::RUNNING || state == PrinterState::PAGE_SHOWING; }
  bool preventAutoSleep() override { return state == PrinterState::RUNNING || state == PrinterState::PAGE_SHOWING; }

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
  OptionPopup optionPopup;

  void beginModeSelect();
  void onModeChosen(bool hotspot);
  bool startAccessPoint();
  void startServices();
  void startMdns();
  void clearJob();  // abort an in-flight job (Left held during transfer)
  void loadQueue();
  void savePageToQueue();
  void showQueueEntry(int index);
  void deleteCurrentPage();
  void clearQueue();
  void openOptions();
  void drawPageHints() const;
  void renderModeSelect() const;
  void renderWaitingScreen() const;
};
