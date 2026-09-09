#include <algorithm>
#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "activities/printer/PrinterClientSession.h"

static uint32_t nowMs = 0;
unsigned long millis() { return nowMs; }
void delay(unsigned long ms) { nowMs += ms; }
uint32_t uptime() { return nowMs / 1000; }

struct Input {
  enum class Button { Back, Left };
  uint32_t backAt = UINT32_MAX;
  bool pressed = false;
  void update() {
    pressed = nowMs >= backAt;
    if (pressed) backAt = UINT32_MAX;
  }
  bool wasAnyPressed() const { return pressed; }
  bool wasPressed(Button button) const { return button == Button::Back && pressed; }
};

struct SocketData {
  std::string input, output;
  size_t pos = 0;
  size_t fragmentSize = 7;
  bool connected = true;
  uint32_t pauseAt = 0;
  uint32_t pauseUntil = 0;
  size_t reads = 0;
};

struct Client {
  std::shared_ptr<SocketData> data = std::make_shared<SocketData>();
  int available() const {
    if (nowMs >= data->pauseAt && nowMs < data->pauseUntil) return 0;
    return static_cast<int>(data->input.size() - data->pos);
  }
  bool connected() const { return data->connected; }
  int read(uint8_t* bytes, size_t size) {
    ++data->reads;
    ++nowMs;
    size = std::min({size, data->input.size() - data->pos, data->fragmentSize});
    memcpy(bytes, data->input.data() + data->pos, size);
    data->pos += size;
    return static_cast<int>(size);
  }
  size_t write(const uint8_t* bytes, size_t size) {
    data->output.append(reinterpret_cast<const char*>(bytes), size);
    return size;
  }
};

using Session = PrinterClientSession<Client, Input>;

struct Display final : ScaledPageSink, IppRequestObserver {
  Session* active = nullptr;
  std::vector<std::string> events;
  bool shown = false;
  uint16_t result = 0xffff;

  void onRequestStarted(uint16_t operation) override {
    if (operation != IppProto::OP_PRINT_JOB) return;
    active->transport.printStarted = true;
    events.emplace_back("receiving");
  }
  void onRequestFinished(uint16_t operation, uint16_t status) override {
    if (operation != IppProto::OP_PRINT_JOB) return;
    events.emplace_back("finished");
    result = status;
  }
  bool onScaledPageBegin(uint32_t, int, int, int, int) override {
    assert(events.size() == 1 && events.front() == "receiving");
    events.emplace_back("page");
    return true;
  }
  bool onScaledRow(int, int, const uint8_t* bits, int width) override {
    assert(width == 8 && bits[0] == 0xff);
    return active->transport.poll();
  }
  void onScaledPageEnd(bool ok, uint32_t) override {
    shown = ok;
    if (ok) events.emplace_back("shown");
  }
};

std::string body(uint8_t operation) {
  std::string out("\1\1\0\0\0\0\0\1\3", 9);
  out[3] = static_cast<char>(operation);
  return out;
}

std::string printBody() {
  std::string out = body(IppProto::OP_PRINT_JOB);
  out.append("UNIRAST\0\0\0\0\1", 12);
  std::string hdr(32, '\0');
  hdr[0] = 8;
  hdr[15] = 8;
  hdr[19] = 1;
  out += hdr;
  out.append("\0\7\0", 3);
  return out;
}

std::string http(const std::string& bytes, size_t extraLength = 0) {
  return "POST /ipp/print HTTP/1.1\r\nContent-Type: application/ipp\r\nContent-Length: " +
         std::to_string(bytes.size() + extraLength) + "\r\n\r\n" + bytes;
}

void runPrint(Client client, Input& input, Display& display) {
  IppServiceConfig cfg;
  Session session(client, input);
  display.active = &session;
  IppPrintService service(cfg, display, 8, 1);
  HttpIppConnection connection(service, cfg.maxJobBytes, &display);
  session.serveOne(connection, uptime);
  display.active = nullptr;
}

int main() {
  Input input;
  IppServiceConfig cfg;
  Display display;
  IppPrintService service(cfg, display, 8, 1);
  HttpIppConnection connection(service, cfg.maxJobBytes, &display);

  // An idle discovery connection must neither enter read() nor wait for Back.
  Client idleClient, printClient;
  idleClient.data->input = http(body(IppProto::OP_GET_PRINTER_ATTRS));
  Session idle(idleClient, input);
  assert(idle.serveOne(connection, uptime));
  const auto timeAfterDiscovery = nowMs;
  assert(!idle.ready());
  assert(nowMs == timeAfterDiscovery);
  printClient.data->input = http(printBody());
  Session printing(printClient, input);
  display.active = &printing;
  assert(printing.ready() && printing.serveOne(connection, uptime));
  assert(display.shown && !printing.transport.userActivity);
  assert(display.events == std::vector<std::string>({"receiving", "page", "shown", "finished"}));
  assert(display.result == IppProto::STATUS_OK);
  assert(nowMs - timeAfterDiscovery < 100);
  assert(!idle.ready() && !printing.ready());

  // Discovery and print can also share a socket. Preserve read-ahead bytes
  // after yielding so the print isn't lost when it arrived in the same read.
  Display pipelinedDisplay;
  IppPrintService pipelinedService(cfg, pipelinedDisplay, 8, 1);
  HttpIppConnection pipelinedConnection(pipelinedService, cfg.maxJobBytes, &pipelinedDisplay);
  Client pipeline;
  pipeline.data->fragmentSize = 512;
  pipeline.data->input = http(body(IppProto::OP_GET_PRINTER_ATTRS)) + http(printBody());
  Session pipelined(pipeline, input);
  pipelinedDisplay.active = &pipelined;
  assert(pipelined.serveOne(pipelinedConnection, uptime));
  assert(pipelined.reader.hasBufferedData() && pipeline.available() == 0);
  assert(pipelined.ready() && pipelined.serveOne(pipelinedConnection, uptime));
  assert(pipelinedDisplay.shown && !pipelined.transport.userActivity);

  // AirPrint also sends chunked bodies with an Expect handshake. Finish the
  // chunk terminator before reusing the same connection for another request.
  Client chunked;
  std::string chunks;
  const auto rasterBody = printBody();
  for (size_t pos = 0; pos < rasterBody.size(); pos += 5) {
    const size_t count = std::min(size_t{5}, rasterBody.size() - pos);
    chunks += std::to_string(count) + "\r\n" + rasterBody.substr(pos, count) + "\r\n";
  }
  chunks += "0\r\n\r\n";
  chunked.data->input =
      "POST /ipp/print HTTP/1.1\r\nContent-Type: application/ipp\r\nTransfer-Encoding: chunked\r\n"
      "Expect: 100-continue\r\n\r\n" +
      chunks;
  Display chunkedDisplay;
  runPrint(chunked, input, chunkedDisplay);
  assert(chunkedDisplay.shown && chunkedDisplay.result == IppProto::STATUS_OK);
  assert(chunked.data->output.starts_with("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK"));

  // Notification precedes raster validation, including a failure before the
  // first page, and no truncated page is reported as shown.
  for (const auto& payload : {body(IppProto::OP_PRINT_JOB), printBody().substr(0, 54)}) {
    Client incomplete;
    incomplete.data->input = http(payload);
    Display failure;
    runPrint(incomplete, input, failure);
    assert(!failure.shown && failure.events.front() == "receiving" && failure.events.back() == "finished");
    assert(failure.result != IppProto::STATUS_OK);
  }

  // A short sender pause must not lose a print; 2.5 seconds was too aggressive
  // once an actual job has started. The idle read is still bounded at 15 sec.
  Client delayed;
  delayed.data->input = http(printBody());
  delayed.data->pauseAt = nowMs + 19;  // After the operation header, before the raster rows.
  delayed.data->pauseUntil = nowMs + 4000;
  Display delayedDisplay;
  runPrint(delayed, input, delayedDisplay);
  assert(delayedDisplay.shown);

  // A complete page arrives before a missing HTTP tail. Back during cleanup
  // must not be necessary to show it, nor change it into an incomplete page.
  Client tail;
  tail.data->input = http(printBody(), 100);
  Display tailDisplay;
  const uint32_t tailStart = nowMs;
  runPrint(tail, input, tailDisplay);
  assert(tailDisplay.shown && tailDisplay.events[2] == "shown");
  assert(nowMs - tailStart >= 15000 && nowMs - tailStart < 15100);

  // Back during the missing tail is recorded after the completed-page callback,
  // so the activity can retain the page and avoid its abort-and-exit path.
  Client backAfterPage;
  backAfterPage.data->input = http(printBody(), 100);
  Display backDisplay;
  IppPrintService backService(cfg, backDisplay, 8, 1);
  HttpIppConnection backConnection(backService, cfg.maxJobBytes, &backDisplay);
  Session backSession(backAfterPage, input);
  backDisplay.active = &backSession;
  input.backAt = nowMs + 100;
  assert(!backSession.serveOne(backConnection, uptime));
  assert(backDisplay.shown && backSession.transport.printStarted && backSession.transport.backPressed);

  // A disconnect must not discard bytes already buffered by the network stack.
  Client closed;
  closed.data->input = http(printBody());
  closed.data->connected = false;
  Display closedDisplay;
  runPrint(closed, input, closedDisplay);
  assert(closedDisplay.shown);

  // Back cancellation is latched: cleanup cannot resume reading the same job.
  Client cancel;
  cancel.data->input = http(printBody(), 100);
  Session cancelled(cancel, input);
  cancelled.transport.beginRequest();
  input.backAt = nowMs + 10;
  nowMs += 10;
  uint8_t byte;
  assert(cancelled.transport.read(&byte, 1) == -1);
  assert(cancelled.transport.backPressed);
  assert(cancelled.transport.read(&byte, 1) == -1 && cancel.data->reads == 0);

  // A sender that keeps trickling bytes still cannot hold the radio forever.
  Session bounded(Client{}, input);
  bounded.transport.beginRequest();
  bounded.transport.printStarted = true;
  nowMs += 120000;
  assert(!bounded.transport.poll() && bounded.transport.timedOut && bounded.transport.deadlineExceeded);

  // Retained idle connections expire independently of printer session renewal.
  assert(idle.expired(nowMs));
  std::puts("PASS: idle discovery does not block printing; pipelined reads survive yielding without button input");
  std::puts("PASS: receiving precedes decoding; failures are reported; complete pages precede HTTP cleanup");
  std::puts("PASS: sender pauses, buffered disconnects, cancellation and transfer deadlines are bounded");
}
