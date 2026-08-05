#include "PrinterActivity.h"

#include <ESPmDNS.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>
#include <WiFi.h>

#include "SilentRestart.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/QrUtils.h"
#include "util/ScreenshotUtil.h"
#include "util/TaskWatchdog.h"

namespace {

constexpr const char* AP_SSID = "CrossPoint-X3";
constexpr const char* AP_HOSTNAME = "crosspoint";
constexpr uint8_t AP_CHANNEL = 1;
constexpr uint8_t AP_MAX_CONNECTIONS = 2;
constexpr uint16_t IPP_PORT = 631;
constexpr unsigned long CLIENT_READ_TIMEOUT_MS = 2500;
constexpr int QR_SIZE = 198;

uint32_t upTimeSeconds() { return millis() / 1000; }

// IppTransport over a connected NetworkClient. While blocked waiting for
// bytes it pumps input (so Back stays responsive) and feeds the watchdog.
class WiFiClientTransport final : public IppTransport {
  NetworkClient& client;
  MappedInputManager& input;

 public:
  bool backPressed = false;

  WiFiClientTransport(NetworkClient& client, MappedInputManager& input) : client(client), input(input) {}

  int read(uint8_t* buf, size_t maxLen) override {
    const unsigned long start = millis();
    while (client.connected()) {
      const int avail = client.available();
      if (avail > 0) {
        const int n = client.read(buf, maxLen);
        return n;
      }
      if (millis() - start > CLIENT_READ_TIMEOUT_MS) return -1;
      resetTaskWatchdogIfSubscribed();
      input.update();
      if (input.wasPressed(MappedInputManager::Button::Back)) {
        backPressed = true;
        return -1;
      }
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

bool PrinterActivity::Sink::onScaledPageBegin(uint32_t pageIndex) {
  LOG_DBG("PRINT", "Receiving page %u, free heap: %d", static_cast<unsigned>(pageIndex), ESP.getFreeHeap());
  return true;
}

void PrinterActivity::Sink::onScaledPageEnd(bool ok, uint32_t pageIndex) {
  (void)pageIndex;
  if (!ok) return;
  activity.pagesReceived++;
  activity.state = PrinterState::PAGE_SHOWING;
  // Called from the main-loop task (inside connection serving), so a blocking
  // render is safe and shows the page before the job response goes out.
  activity.requestUpdateAndWait();
}

void PrinterActivity::onEnter() {
  Activity::onEnter();
  LOG_DBG("PRINT", "Free heap at onEnter: %d", ESP.getFreeHeap());

  state = PrinterState::STARTING;
  pagesReceived = 0;
  exitRequested = false;
  requestUpdateAndWait();

  const int pageW = renderer.getScreenWidth();
  const int pageH = renderer.getScreenHeight();
  const size_t bitsSize = static_cast<size_t>((pageW + 7) / 8) * pageH;
  pageBits = makeUniqueNoThrow<uint8_t[]>(bitsSize);
  if (!pageBits) {
    LOG_ERR("PRINT", "OOM: %u byte page buffer", static_cast<unsigned>(bitsSize));
    state = PrinterState::FAILED;
    onGoHome(HomeMenuItem::PRINTER);
    return;
  }

  if (!startAccessPoint()) {
    state = PrinterState::FAILED;
    onGoHome(HomeMenuItem::PRINTER);
    return;
  }

  snprintf(printerUri, sizeof(printerUri), "ipp://%s:%u/ipp/print", apIp.c_str(), IPP_PORT);
  snprintf(moreInfoUrl, sizeof(moreInfoUrl), "http://%s/", apIp.c_str());

  IppServiceConfig cfg;
  cfg.printerName = "CrossPoint X3";
  cfg.makeAndModel = "CrossPoint E-Reader Printer";
  cfg.printerUri = printerUri;
  cfg.moreInfoUrl = moreInfoUrl;

  sink = makeUniqueNoThrow<Sink>(*this);
  if (sink) service = makeUniqueNoThrow<IppPrintService>(cfg, *sink, pageBits.get(), pageW, pageH);
  if (service) connection = makeUniqueNoThrow<HttpIppConnection>(*service, cfg.maxJobBytes);
  if (!connection) {
    LOG_ERR("PRINT", "OOM: IPP service");
    state = PrinterState::FAILED;
    onGoHome(HomeMenuItem::PRINTER);
    return;
  }

  startMdns();
  server.begin(IPP_PORT);
  server.setNoDelay(true);
  serverStarted = true;

  state = PrinterState::RUNNING;
  LOG_DBG("PRINT", "IPP server on %s, free heap: %d", printerUri, ESP.getFreeHeap());
  requestUpdate();
}

bool PrinterActivity::startAccessPoint() {
  LOG_DBG("PRINT", "Starting AP...");
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
  apIp = ipStr;
  apSsid = AP_SSID;
  LOG_DBG("PRINT", "AP up: %s @ %s", apSsid.c_str(), apIp.c_str());
  return true;
}

void PrinterActivity::startMdns() {
  MDNS.end();
  if (!MDNS.begin(AP_HOSTNAME)) {
    // Direct ipp://<ip> printing still works without discovery.
    LOG_ERR("PRINT", "mDNS failed to start");
    return;
  }
  MDNS.addService("ipp", "tcp", IPP_PORT);
  MDNS.addServiceTxt("ipp", "tcp", "txtvers", "1");
  MDNS.addServiceTxt("ipp", "tcp", "qtotal", "1");
  MDNS.addServiceTxt("ipp", "tcp", "rp", "ipp/print");
  MDNS.addServiceTxt("ipp", "tcp", "ty", "CrossPoint X3");
  MDNS.addServiceTxt("ipp", "tcp", "note", "E-Reader");
  MDNS.addServiceTxt("ipp", "tcp", "pdl", "image/urf,image/pwg-raster");
  MDNS.addServiceTxt("ipp", "tcp", "URF", "V1.4,W8,SRGB24,CP1,RS300,DM1");
  MDNS.addServiceTxt("ipp", "tcp", "Color", "F");
  MDNS.addServiceTxt("ipp", "tcp", "Duplex", "F");
  MDNS.addServiceTxt("ipp", "tcp", "UUID", "8e7a24f2-1f0b-4c9e-9d3a-c0ffee000e01");
  MDNS.addServiceTxt("ipp", "tcp", "adminurl", static_cast<const char*>(moreInfoUrl));
  LOG_DBG("PRINT", "mDNS: _ipp._tcp advertised");
}

void PrinterActivity::onExit() {
  Activity::onExit();
  if (serverStarted) server.end();
  MDNS.end();
  connection.reset();
  service.reset();
  sink.reset();
  pageBits.reset();

  // Same convention as CrossPointWebServerActivity: restart silently after
  // WiFi use so the radio and heap come back to a known state.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.softAPdisconnect(true);
    delay(30);
    silentRestart();
  }
}

void PrinterActivity::loop() {
  if (state != PrinterState::RUNNING && state != PrinterState::PAGE_SHOWING) return;

  mappedInput.update();
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onGoHome(HomeMenuItem::PRINTER);
    return;
  }
  if (state == PrinterState::PAGE_SHOWING && mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    savePageToInbox();
  }
  if (savedBannerUntil != 0 && millis() > savedBannerUntil) {
    savedBannerUntil = 0;
    requestUpdate();  // redraw the page without the banner
  }

  NetworkClient client = server.accept();
  if (client) {
    LOG_DBG("PRINT", "client connected");
    client.setNoDelay(true);
    WiFiClientTransport transport(client, mappedInput);
    connection->serve(transport, upTimeSeconds);
    client.stop();
    LOG_DBG("PRINT", "client done, free heap: %d", ESP.getFreeHeap());
    if (transport.backPressed) {
      onGoHome(HomeMenuItem::PRINTER);
    }
  }
}

void PrinterActivity::savePageToInbox() {
  char path[48];
  snprintf(path, sizeof(path), "/printouts/print-%lu.bmp", millis());
  // The shown page IS the framebuffer, so the screenshot writer does the work.
  if (ScreenshotUtil::saveFramebufferAsBmp(path, renderer.getFrameBuffer(), renderer.getDisplayWidth(),
                                           renderer.getDisplayHeight())) {
    LOG_DBG("PRINT", "Saved %s", path);
    savedBannerUntil = millis() + 1500;
    requestUpdate();
  } else {
    LOG_ERR("PRINT", "Save failed: %s", path);
  }
}

void PrinterActivity::renderWaitingScreen() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_PRINTER_MODE), nullptr);
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    apSsid.c_str());

  const int height10 = renderer.getLineHeight(UI_10_FONT_ID);
  int y = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing * 2;

  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, tr(STR_CONNECT_WIFI_HINT), true, EpdFontFamily::BOLD);
  y += height10 + metrics.verticalSpacing * 2;

  const std::string wifiConfig = std::string("WIFI:T:nopass;S:") + apSsid + ";;";
  QrUtils::drawQrCode(renderer, Rect(metrics.contentSidePadding, y, QR_SIZE, QR_SIZE), wifiConfig);
  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding + QR_SIZE + metrics.verticalSpacing, y + 80,
                    apSsid.c_str());
  y += QR_SIZE + metrics.verticalSpacing * 2;

  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, tr(STR_PRINTER_PRINT_HINT), true,
                    EpdFontFamily::BOLD);
  y += height10 + metrics.verticalSpacing;
  renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y, printerUri);

  const auto labels = mappedInput.mapLabels(tr(STR_EXIT), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void PrinterActivity::renderPage() const {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  const int stride = (w + 7) / 8;
  const uint8_t* bits = pageBits.get();

  for (int y = 0; y < h; y++) {
    const uint8_t* row = bits + static_cast<size_t>(y) * stride;
    for (int x = 0; x < w; x++) {
      if (row[x >> 3] & (0x80 >> (x & 7))) renderer.drawPixel(x, y, true);
    }
  }

  if (savedBannerUntil != 0) {
    const int height10 = renderer.getLineHeight(UI_10_FONT_ID);
    renderer.fillRect(0, h - height10 - 8, w, height10 + 8, false);
    renderer.drawCenteredText(UI_10_FONT_ID, h - height10 - 4, tr(STR_PRINTER_PAGE_SAVED), true, EpdFontFamily::BOLD);
  }
}

void PrinterActivity::render(RenderLock&&) {
  renderer.clearScreen();
  switch (state) {
    case PrinterState::STARTING:
      renderer.drawCenteredText(UI_10_FONT_ID, (renderer.getScreenHeight() - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                                tr(STR_PRINTER_STARTING));
      break;
    case PrinterState::RUNNING:
      renderWaitingScreen();
      break;
    case PrinterState::PAGE_SHOWING:
      renderPage();
      break;
    case PrinterState::FAILED:
      break;
  }
  renderer.displayBuffer();
}
