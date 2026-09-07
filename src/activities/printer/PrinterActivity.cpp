#include "PrinterActivity.h"

#include <Bitmap.h>
#include <ESPmDNS.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <WiFi.h>
#include <mdns.h>  // subtype API; ESPmDNS does not expose it

#include "CrossPointSettings.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "components/icons/penguin.h"
#include "fontIds.h"
#include "util/QrUtils.h"
#include "util/ScreenshotUtil.h"
#include "util/TaskWatchdog.h"

namespace {

constexpr const char* AP_SSID = "literate-penguin";
constexpr const char* HOSTNAME = "penguin";
constexpr const char* PRINTER_NAME = "penguin";
constexpr uint8_t AP_CHANNEL = 1;
constexpr uint8_t AP_MAX_CONNECTIONS = 2;
constexpr uint16_t IPP_PORT = 631;
constexpr unsigned long CLIENT_READ_TIMEOUT_MS = 2500;
constexpr int QR_SIZE = 198;
constexpr int MODE_ITEM_COUNT = 2;
constexpr const char* QUEUE_DIR = "/printouts";
constexpr int QUEUE_RESERVE = 16;
constexpr int PENGUIN_ART_SIZE = 96;

uint32_t upTimeSeconds() { return millis() / 1000; }

// IppTransport over a connected NetworkClient. While waiting for bytes it
// pumps input (Back aborts-and-exits, Left aborts-and-stays) and the watchdog.
class WiFiClientTransport final : public IppTransport {
  static constexpr int YIELD_EVERY_READS = 32;

  NetworkClient& client;
  MappedInputManager& input;
  int readsSinceYield = 0;

  bool pollInput() {
    input.update();
    userActivity = userActivity || input.wasAnyPressed();
    backPressed = backPressed || input.wasPressed(MappedInputManager::Button::Back);
    clearPressed = clearPressed || input.wasPressed(MappedInputManager::Button::Left);
    return !backPressed && !clearPressed;
  }

 public:
  bool backPressed = false;
  bool clearPressed = false;
  bool userActivity = false;

  WiFiClientTransport(NetworkClient& client, MappedInputManager& input) : client(client), input(input) {}

  int read(uint8_t* buf, size_t maxLen) override {
    const unsigned long start = millis();
    while (client.connected()) {
      const int avail = client.available();
      if (avail > 0) {
        // A real job streams for many seconds without ever going idle, so the
        // watchdog must be fed on the data path too — not just when waiting.
        resetTaskWatchdogIfSubscribed();
        if (++readsSinceYield >= YIELD_EVERY_READS) {
          readsSinceYield = 0;
          delay(1);
          if (!pollInput()) return -1;
        }
        return client.read(buf, maxLen);
      }
      if (millis() - start > CLIENT_READ_TIMEOUT_MS) return -1;
      resetTaskWatchdogIfSubscribed();
      if (!pollInput()) return -1;
      delay(2);
    }
    return 0;
  }

  bool write(const uint8_t* buf, size_t len) override {
    while (len > 0) {
      const size_t n = client.write(buf, len);
      if (n == 0) return false;
      buf += n;
      len -= n;
    }
    return true;
  }
};

}  // namespace

bool PrinterActivity::Sink::onScaledPageBegin(uint32_t pageIndex, int boxX, int boxY, int boxW, int boxH) {
  (void)boxX;
  (void)boxY;
  (void)boxW;
  (void)boxH;
  LOG_DBG("PRINT", "Receiving page %u, free heap: %d", static_cast<unsigned>(pageIndex), ESP.getFreeHeap());
  activity.state = PrinterState::PAGE_SHOWING;
  activity.renderer.clearScreen();
  return true;
}

bool PrinterActivity::Sink::onScaledRow(int y, int xOffset, const uint8_t* rowBits, int width) {
  // Rows accumulate silently in the framebuffer; the finished page is shown in
  // a single refresh at page end. Intermediate refreshes cost ~0.5s each on
  // e-ink, which made the whole print feel slow.
  GfxRenderer& renderer = activity.renderer;
  for (int x = 0; x < width; x++) {
    if (rowBits[x >> 3] & (0x80 >> (x & 7))) renderer.drawPixel(xOffset + x, y, true);
  }
  resetTaskWatchdogIfSubscribed();
  return true;
}

void PrinterActivity::Sink::onScaledPageEnd(bool ok, uint32_t pageIndex) {
  (void)pageIndex;
  if (!ok) {
    activity.state = PrinterState::RUNNING;
    activity.requestUpdate();
    return;
  }
  // Persist before stamping hints so the stored page is clean, then show the
  // hints over it — the page IS the framebuffer, there is no copy to repaint.
  activity.savePageToQueue();
  activity.renewTimeout();
  activity.drawPageHints();
  LOG_DBG("PRINT", "Page complete, free heap: %d", ESP.getFreeHeap());
}

void PrinterActivity::onEnter() {
  Activity::onEnter();
  LOG_DBG("PRINT", "Free heap at onEnter: %d", ESP.getFreeHeap());

  beginModeSelect();
}

void PrinterActivity::beginModeSelect() {
  state = PrinterState::MODE_SELECT;
  modeIndex = 0;
  requestUpdate();
}

void PrinterActivity::onModeChosen(const bool hotspot) {
  isApMode = hotspot;
  state = PrinterState::STARTING;
  requestUpdateAndWait();

  if (hotspot) {
    if (!startAccessPoint()) {
      failStart();
      return;
    }
    startServices();
    return;
  }

  // Join Network: WifiSelectionActivity handles scan/pick/password/connect.
  WiFi.mode(WIFI_STA);
  state = PrinterState::WIFI_SELECTING;
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             beginModeSelect();
                             return;
                           }
                           const auto& wifi = std::get<WifiResult>(result.data);
                           netSsid = wifi.ssid;
                           netIp = wifi.ip;
                           startServices();
                         });
}

bool PrinterActivity::startAccessPoint() {
  LOG_DBG("PRINT", "Starting AP '%s'...", AP_SSID);
  WiFi.mode(WIFI_AP);
  delay(100);
  if (!WiFi.softAP(AP_SSID, nullptr, AP_CHANNEL, false, AP_MAX_CONNECTIONS)) {
    LOG_ERR("PRINT", "Failed to start AP");
    return false;
  }
  delay(100);
  const IPAddress ip = WiFi.softAPIP();
  char ipStr[16];
  snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  netIp = ipStr;
  netSsid = AP_SSID;
  LOG_DBG("PRINT", "AP up: %s @ %s", netSsid.c_str(), netIp.c_str());
  return true;
}

void PrinterActivity::updateAddress() {
  const IPAddress ip = isApMode ? WiFi.softAPIP() : WiFi.localIP();
  netIp = ip.toString().c_str();
  snprintf(printerUri, sizeof(printerUri), "ipp://%s:%u/ipp/print", netIp.c_str(), IPP_PORT);
  snprintf(moreInfoUrl, sizeof(moreInfoUrl), "http://%s/", netIp.c_str());
}

void PrinterActivity::startServices() {
  // Match the web server's reachability policy, scoped to this timed session.
  previousWifiSleep = WiFi.getSleep();
  wifiSleepChanged = WiFi.setSleep(false);
  if (!wifiSleepChanged) {
    LOG_ERR("PRINT", "Could not disable WiFi modem sleep");
    failStart();
    return;
  }
  if (!isApMode) WiFi.setAutoReconnect(true);
  updateAddress();

  IppServiceConfig cfg;
  cfg.printerName = PRINTER_NAME;
  cfg.makeAndModel = "CrossPoint X3 E-Reader";
  cfg.printerUri = printerUri;
  cfg.moreInfoUrl = moreInfoUrl;

  sink = makeUniqueNoThrow<Sink>(*this);
  if (sink)
    service = makeUniqueNoThrow<IppPrintService>(cfg, *sink, renderer.getScreenWidth(), renderer.getScreenHeight());
  if (service) connection = makeUniqueNoThrow<HttpIppConnection>(*service, cfg.maxJobBytes);
  if (!connection) {
    LOG_ERR("PRINT", "OOM: IPP service");
    failStart();
    return;
  }

  loadQueue();  // printouts from earlier sessions are part of the queue
  startMdns();
  server.begin(IPP_PORT);
  server.setNoDelay(true);
  serverStarted = static_cast<bool>(server);
  if (!serverStarted) {
    LOG_ERR("PRINT", "Could not start IPP listener");
    failStart();
    return;
  }

  state = PrinterState::RUNNING;
  networkReady = true;
  idleTimer.start(millis(), SETTINGS.getSleepTimeoutMs());
  shownMinutes = idleTimer.remainingMinutes(millis());
  LOG_DBG("PRINT", "IPP server on %s, free heap: %d", printerUri, ESP.getFreeHeap());
  requestUpdate();
}

void PrinterActivity::failStart() {
  server.end();
  serverStarted = false;
  MDNS.end();
  connection.reset();
  service.reset();
  sink.reset();
  if (wifiSleepChanged) WiFi.setSleep(previousWifiSleep);
  wifiSleepChanged = false;
  WiFi.mode(WIFI_OFF);
  networkReady = false;
  state = PrinterState::FAILED;
  requestUpdate();
}

void PrinterActivity::startMdns() {
  MDNS.end();
  discoveryReady = false;
  if (!MDNS.begin(HOSTNAME)) {
    // Direct ipp://<ip> printing still works without discovery.
    LOG_ERR("PRINT", "mDNS failed to start");
    return;
  }
  MDNS.addService("ipp", "tcp", IPP_PORT);
  MDNS.addServiceTxt("ipp", "tcp", "txtvers", "1");
  MDNS.addServiceTxt("ipp", "tcp", "qtotal", "1");
  MDNS.addServiceTxt("ipp", "tcp", "rp", "ipp/print");
  MDNS.addServiceTxt("ipp", "tcp", "ty", PRINTER_NAME);
  MDNS.addServiceTxt("ipp", "tcp", "note", "E-Reader");
  MDNS.addServiceTxt("ipp", "tcp", "pdl", "image/urf,image/pwg-raster");
  MDNS.addServiceTxt("ipp", "tcp", "URF", "V1.4,W8,SRGB24,CP1,RS300,DM1");
  MDNS.addServiceTxt("ipp", "tcp", "Color", "F");
  MDNS.addServiceTxt("ipp", "tcp", "Duplex", "F");
  MDNS.addServiceTxt("ipp", "tcp", "UUID", "8e7a24f2-1f0b-4c9e-9d3a-c0ffee000e01");
  MDNS.addServiceTxt("ipp", "tcp", "adminurl", static_cast<const char*>(moreInfoUrl));

  // AirPrint clients only treat an _ipp._tcp service as a driverless printer
  // when it also advertises the "_universal" subtype; without it macOS lists
  // the printer but demands a driver. ESPmDNS has no subtype API, so call the
  // underlying ESP-IDF mDNS component directly (nullptr instance/hostname =
  // first matching service on the local host).
  const esp_err_t subErr = mdns_service_subtype_add_for_host(nullptr, "_ipp", "_tcp", nullptr, "_universal");
  if (subErr != ESP_OK) {
    LOG_ERR("PRINT", "mDNS: _universal subtype failed (%d) — client may ask for a driver", subErr);
  }
  discoveryReady = subErr == ESP_OK;
  LOG_DBG("PRINT", "mDNS: _ipp._tcp,_universal advertised as '%s'", PRINTER_NAME);
}

void PrinterActivity::onExit() {
  Activity::onExit();
  if (serverStarted) server.end();
  MDNS.end();
  connection.reset();
  service.reset();
  sink.reset();
  if (wifiSleepChanged) WiFi.setSleep(previousWifiSleep);

  // Same convention as CrossPointWebServerActivity: restart silently after
  // WiFi use so the radio and heap come back to a known state.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    if (isApMode) {
      WiFi.softAPdisconnect(true);
    } else {
      WiFi.disconnect(false);
    }
    delay(30);
    silentRestart();
  }
}

void PrinterActivity::clearJob() {
  LOG_DBG("PRINT", "Clear: dropping shown page, back to waiting");
  state = PrinterState::RUNNING;
  requestUpdate();
}

void PrinterActivity::renewTimeout() { idleTimer.renew(millis()); }

void PrinterActivity::updateNetwork() {
  const unsigned long now = millis();
  if (now - lastNetworkCheck < 1000) return;
  lastNetworkCheck = now;
  const bool connected = isApMode ? (WiFi.getMode() & WIFI_AP) != 0 : WiFi.status() == WL_CONNECTED;
  if (!connected) {
    if (networkReady) {
      LOG_INF("PRINT", "Network lost; waiting for reconnect");
      server.end();
      serverStarted = false;
      MDNS.end();
      networkReady = false;
      // Printouts are saved, so a lost network can replace the shown page with
      // an honest status. The user can still browse the saved queue.
      state = PrinterState::RUNNING;
      requestUpdate();
    }
    return;
  }
  const IPAddress ip = isApMode ? WiFi.softAPIP() : WiFi.localIP();
  if (networkReady && netIp == ip.toString().c_str()) return;
  updateAddress();
  server.end();
  server.begin(IPP_PORT);
  server.setNoDelay(true);
  serverStarted = static_cast<bool>(server);
  networkReady = serverStarted;
  if (networkReady) {
    startMdns();
    LOG_INF("PRINT", "Network restored: %s", printerUri);
  } else {
    LOG_ERR("PRINT", "Could not restart IPP listener");
    failStart();
  }
  requestUpdate();
}

void PrinterActivity::loop() {
  if (state == PrinterState::MODE_SELECT) {
    auto selectCurrent = [this] { onModeChosen(modeIndex == 1); };

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      onGoHome(HomeMenuItem::PRINTER);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      selectCurrent();
      return;
    }
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight =
        renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
    switch (handleListTouch(modeIndex, MODE_ITEM_COUNT, contentTop, contentHeight, true)) {
      case ListTouchResult::Activated:
        selectCurrent();
        return;
      case ListTouchResult::Consumed:
        return;
      case ListTouchResult::None:
        break;
    }
    buttonNavigator.onNext([this] {
      modeIndex = ButtonNavigator::nextIndex(modeIndex, MODE_ITEM_COUNT);
      requestUpdate();
    });
    buttonNavigator.onPrevious([this] {
      modeIndex = ButtonNavigator::previousIndex(modeIndex, MODE_ITEM_COUNT);
      requestUpdate();
    });
    return;
  }

  if (state == PrinterState::FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) onGoHome(HomeMenuItem::PRINTER);
    return;
  }
  if (!isSessionActive()) return;
  // Timer renders, queue browsing, and incoming rows share one framebuffer.
  RenderLock lock(*this);

  // main.cpp already sampled input. Sampling again here erases press events.
  if (mappedInput.wasAnyPressed()) renewTimeout();
  if (idleTimer.expired(millis())) return;
  updateNetwork();
  if (!isSessionActive()) return;

  const uint32_t minutes = idleTimer.remainingMinutes(millis());
  if (minutes != shownMinutes) {
    shownMinutes = minutes;
    requestUpdate();
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    // Back steps out one level: from a printout back to the printer screen,
    // and only from the printer screen out to home.
    if (state == PrinterState::PAGE_SHOWING) {
      state = PrinterState::RUNNING;
      requestUpdate();
    } else {
      onGoHome(HomeMenuItem::PRINTER);
    }
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    // Redraw even within the same minute to acknowledge the reset.
    requestUpdate();
    return;
  }
  // Front Left/Right and the side Up/Down buttons both navigate, matching
  // BmpViewerActivity: Left|Up = previous, Right|Down = next.
  const bool prevPressed = mappedInput.wasPressed(MappedInputManager::Button::Left) ||
                           mappedInput.wasPressed(MappedInputManager::Button::Up);
  const bool nextPressed = mappedInput.wasPressed(MappedInputManager::Button::Right) ||
                           mappedInput.wasPressed(MappedInputManager::Button::Down);

  if (state == PrinterState::PAGE_SHOWING) {
    if (prevPressed && queueIndex > 0) {
      showQueueEntry(queueIndex - 1);
      return;
    }
    if (nextPressed && queueIndex < static_cast<int>(queue.size()) - 1) {
      showQueueEntry(queueIndex + 1);
      return;
    }
  } else if (!queue.empty() && (prevPressed || nextPressed)) {
    // Printer screen: step into the stored printouts. Deliberately unlabelled —
    // the hints stay off this screen, the buttons still work.
    showQueueEntry(queueIndex < 0 ? static_cast<int>(queue.size()) - 1 : queueIndex);
    return;
  }
  if (!networkReady) return;
  NetworkClient client = server.accept();
  if (client) {
    LOG_DBG("PRINT", "client connected");
    client.setNoDelay(true);
    WiFiClientTransport transport(client, mappedInput);
    // An open print dialog must not monopolize the loop with keep-alive probes.
    // Finish the request, advertise Connection: close, then service the timer.
    connection->serve(transport, upTimeSeconds, false);
    if (transport.userActivity) {
      renewTimeout();
      requestUpdate();
    }
    client.stop();
    LOG_DBG("PRINT", "client done, free heap: %d", ESP.getFreeHeap());
    if (transport.backPressed) {
      onGoHome(HomeMenuItem::PRINTER);
    } else if (transport.clearPressed) {
      clearJob();
    }
  }
}

void PrinterActivity::loadQueue() {
  queue.clear();
  queue.reserve(QUEUE_RESERVE);

  auto dir = Storage.open(QUEUE_DIR);
  if (!dir || !dir.isDirectory()) return;

  char name[128];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      file.getName(name, sizeof(name));
      const std::string fname(name);
      if (fname.size() > 4 && fname[0] != '.' && fname.compare(fname.size() - 4, 4, ".bmp") == 0) {
        queue.push_back(fname);
      }
    }
    file.close();
  }
  dir.close();

  FsHelpers::sortFileList(queue);
  queueIndex = queue.empty() ? -1 : static_cast<int>(queue.size()) - 1;
  LOG_DBG("PRINT", "Queue: %d printout(s)", static_cast<int>(queue.size()));
}

void PrinterActivity::savePageToQueue() {
  char path[64];
  snprintf(path, sizeof(path), "%s/print-%lu.bmp", QUEUE_DIR, millis());
  // The shown page IS the framebuffer, so the screenshot writer does the work.
  if (!ScreenshotUtil::saveFramebufferAsBmp(path, renderer.getFrameBuffer(), renderer.getDisplayWidth(),
                                            renderer.getDisplayHeight())) {
    LOG_ERR("PRINT", "Save failed: %s", path);
    return;
  }
  const char* slash = strrchr(path, '/');
  queue.push_back(slash ? slash + 1 : path);
  queueIndex = static_cast<int>(queue.size()) - 1;
  LOG_DBG("PRINT", "Saved %s (queue: %d)", path, static_cast<int>(queue.size()));
}

void PrinterActivity::showQueueEntry(const int index) {
  if (index < 0 || index >= static_cast<int>(queue.size())) return;
  queueIndex = index;

  const std::string path = std::string(QUEUE_DIR) + "/" + queue[index];
  HalFile file;
  if (!Storage.openFileForRead("PRINT", path, file)) {
    LOG_ERR("PRINT", "Cannot open %s", path.c_str());
    return;
  }

  Bitmap bitmap(file, true);
  renderer.clearScreen();
  if (bitmap.parseHeaders() == BmpReaderError::Ok) {
    const int x = (renderer.getScreenWidth() - bitmap.getWidth()) / 2;
    const int y = (renderer.getScreenHeight() - bitmap.getHeight()) / 2;
    renderer.drawBitmap(bitmap, x < 0 ? 0 : x, y < 0 ? 0 : y, renderer.getScreenWidth(), renderer.getScreenHeight(), 0,
                        0);
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_INVALID_BMP_FILE));
  }
  file.close();

  state = PrinterState::PAGE_SHOWING;
  drawPageHints();
}

void PrinterActivity::drawPageHints() const {
  const bool hasPrev = queueIndex > 0;
  const bool hasNext = queueIndex >= 0 && queueIndex < static_cast<int>(queue.size()) - 1;
  drawTimeout();
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), tr(STR_PRINTER_RESET_TIMER), hasPrev ? "<" : "", hasNext ? ">" : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  // The side buttons navigate too — label them so that is discoverable.
  GUI.drawSideButtonHints(renderer, hasPrev ? "<" : "", hasNext ? ">" : "");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void PrinterActivity::drawTimeout() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int y = renderer.getScreenHeight() - metrics.buttonHintsHeight - lineHeight - 12;
  renderer.fillRect(0, y - 4, width, lineHeight + 12, false);
  char text[96];
  snprintf(text, sizeof(text), tr(STR_PRINTER_TIMEOUT_FORMAT),
           static_cast<unsigned>(idleTimer.remainingMinutes(millis())));
  renderer.drawCenteredText(SMALL_FONT_ID, y, text);
}

bool PrinterActivity::prepareSleepScreen(bool fromTimeout) {
  if (!fromTimeout || !isSessionActive()) return false;
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, height / 3, tr(STR_PRINTER_ASLEEP), true, EpdFontFamily::BOLD);
  UITheme::drawCenteredWrappedText(renderer, Rect{24, height / 3 + 50, width - 48, 100}, UI_10_FONT_ID,
                                   tr(STR_PRINTER_TIMEOUT_REASON), 3);
  UITheme::drawCenteredWrappedText(renderer, Rect{24, height / 2 + 60, width - 48, height / 4}, UI_10_FONT_ID,
                                   tr(STR_PRINTER_WAKE_HINT), 4);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  return true;
}

void PrinterActivity::renderModeSelect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_PRINTER_MODE));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  static constexpr StrId modeItems[MODE_ITEM_COUNT] = {StrId::STR_JOIN_NETWORK, StrId::STR_CREATE_HOTSPOT};
  static constexpr StrId modeDescs[MODE_ITEM_COUNT] = {StrId::STR_JOIN_DESC, StrId::STR_HOTSPOT_DESC};
  static constexpr UIIcon modeIcons[MODE_ITEM_COUNT] = {UIIcon::Wifi, UIIcon::Hotspot};

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, MODE_ITEM_COUNT, modeIndex,
      [](int index) { return std::string(I18N.get(modeItems[index])); },
      [](int index) { return std::string(I18N.get(modeDescs[index])); }, [](int index) { return modeIcons[index]; });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void PrinterActivity::renderWaitingScreen() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_PRINTER_MODE), nullptr);
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    netSsid.c_str());

  const int height10 = renderer.getLineHeight(UI_10_FONT_ID);
  int y = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing * 2;

  if (!networkReady) {
    UITheme::drawCenteredWrappedText(renderer, Rect{24, y, pageWidth - 48, 100}, UI_10_FONT_ID,
                                     tr(STR_PRINTER_DISCONNECTED), 3);
    drawTimeout();
    const auto labels = mappedInput.mapLabels(tr(STR_EXIT), tr(STR_PRINTER_RESET_TIMER), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }

  if (isApMode) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, tr(STR_CONNECT_WIFI_HINT), true,
                      EpdFontFamily::BOLD);
    y += height10 + metrics.verticalSpacing * 2;
    const std::string wifiConfig = std::string("WIFI:T:nopass;S:") + netSsid + ";;";
    QrUtils::drawQrCode(renderer, Rect(metrics.contentSidePadding, y, QR_SIZE, QR_SIZE), wifiConfig);
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding + QR_SIZE + metrics.verticalSpacing, y + 80,
                      netSsid.c_str());
    y += QR_SIZE + metrics.verticalSpacing * 2;
  }

  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, tr(STR_PRINTER_PRINT_HINT), true,
                    EpdFontFamily::BOLD);
  y += height10 + metrics.verticalSpacing;
  renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y, printerUri);
  y += renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing * 3;
  if (!discoveryReady) {
    UITheme::drawCenteredWrappedText(renderer, Rect{24, y, pageWidth - 48, 60}, SMALL_FONT_ID,
                                     tr(STR_PRINTER_DISCOVERY_UNAVAILABLE), 2);
    y += 60;
  }

  // The penguin waiting with a page: this screen is where you sit while
  // nothing is happening, so give it something to look at.
  const int artX = (pageWidth - PENGUIN_ART_SIZE) / 2;
  const int bottomLimit = renderer.getScreenHeight() - metrics.buttonHintsHeight - height10 * 2;
  if (y + PENGUIN_ART_SIZE <= bottomLimit) {
    renderer.drawIcon(PenguinArt, artX, y, PENGUIN_ART_SIZE);
    y += PENGUIN_ART_SIZE + metrics.verticalSpacing;
    renderer.drawCenteredText(SMALL_FONT_ID, y, tr(STR_PRINTER_WAITING), true, EpdFontFamily::ITALIC);
  }

  drawTimeout();
  const auto labels = mappedInput.mapLabels(tr(STR_EXIT), tr(STR_PRINTER_RESET_TIMER), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void PrinterActivity::render(RenderLock&&) {
  if (state != PrinterState::PAGE_SHOWING) renderer.clearScreen();
  switch (state) {
    case PrinterState::MODE_SELECT:
      renderModeSelect();
      renderer.displayBuffer();
      break;
    case PrinterState::STARTING:
      renderer.drawCenteredText(UI_10_FONT_ID, (renderer.getScreenHeight() - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                                tr(STR_PRINTER_STARTING));
      renderer.displayBuffer();
      break;
    case PrinterState::RUNNING:
      renderWaitingScreen();
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      break;
    case PrinterState::PAGE_SHOWING:
      // Preserve the printout and refresh only its countdown footer.
      drawTimeout();
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      break;
    case PrinterState::WIFI_SELECTING:
      break;
    case PrinterState::FAILED:
      UITheme::drawCenteredWrappedText(renderer, Rect{24, 120, renderer.getScreenWidth() - 48, 180}, UI_10_FONT_ID,
                                       tr(STR_PRINTER_START_FAILED), 4);
      {
        const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
        GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      }
      renderer.displayBuffer();
      break;
  }
}
