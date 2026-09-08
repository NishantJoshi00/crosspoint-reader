#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>

#include "HttpIppConnection.h"
#include "IppParser.h"

class MemoryTransport final : public IppTransport {
  std::string input;
  size_t pos = 0;

 public:
  std::string output;
  size_t readLimit = SIZE_MAX;
  explicit MemoryTransport(std::string data) : input(std::move(data)) {}
  int read(uint8_t* buf, size_t maxLen) override {
    const size_t len = std::min({maxLen, input.size() - pos, readLimit});
    memcpy(buf, input.data() + pos, len);
    pos += len;
    return static_cast<int>(len);
  }
  bool write(const uint8_t* buf, size_t len) override {
    output.append(reinterpret_cast<const char*>(buf), len);
    return true;
  }
};

class NoPages final : public ScaledPageSink {
 public:
  bool onScaledPageBegin(uint32_t, int, int, int, int) override {
    assert(false && "Discovery must not produce a page or renew the timer");
    return false;
  }
  bool onScaledRow(int, int, const uint8_t*, int) override { return false; }
  void onScaledPageEnd(bool, uint32_t) override {}
};

class PrintedPage final : public ScaledPageSink {
 public:
  bool complete = false;
  int rows = 0;
  bool onScaledPageBegin(uint32_t, int, int, int w, int h) override { return w == 8 && h == 1; }
  bool onScaledRow(int, int, const uint8_t* bits, int width) override {
    assert(width == 8 && bits[0] == 0xff);  // Eight black pixels survive decoding and scaling.
    ++rows;
    return true;
  }
  void onScaledPageEnd(bool ok, uint32_t) override { complete = ok; }
};

static int requestsHandled = 0;
uint32_t uptime() { return static_cast<uint32_t>(++requestsHandled); }

int main() {
  NoPages sink;
  IppServiceConfig cfg;
  IppPrintService service(cfg, sink, 528, 792);
  HttpIppConnection connection(service, cfg.maxJobBytes);
  const char attributes[] = {1, 1, 0, 0x0b, 0, 0, 0, 1, 3};  // Get-Printer-Attributes, end-of-attributes.
  const std::string request =
      "POST /ipp/print HTTP/1.1\r\nContent-Type: application/ipp\r\nContent-Length: 9\r\n"
      "Connection: keep-alive\r\n\r\n" +
      std::string(attributes, sizeof(attributes));

  // Even pipelined discovery requests must return control after one response
  // when the printer's timed session disables persistent connections.
  MemoryTransport bounded(request + request);
  connection.serve(bounded, uptime, false);
  assert(requestsHandled == 1);
  assert(bounded.output.starts_with("HTTP/1.1 200 OK\r\n"));
  assert(bounded.output.find("Connection: close\r\n") != std::string::npos);
  assert(bounded.output.find("HTTP/1.1", 1) == std::string::npos);
  const size_t bodyStart = bounded.output.find("\r\n\r\n") + 4;
  assert(bounded.output.size() > bodyStart + 8);
  assert(bounded.output[bodyStart + 2] == 0 && bounded.output[bodyStart + 3] == 0);  // IPP success.

  // The default keeps the host harness's existing persistent behavior.
  requestsHandled = 0;
  MemoryTransport persistent(request + request);
  connection.serve(persistent, uptime);
  assert(requestsHandled == 2);
  assert(persistent.output.find("Connection: keep-alive\r\n") != std::string::npos);

  MemoryTransport health("GET / HTTP/1.1\r\n\r\nGET / HTTP/1.1\r\n\r\n");
  connection.serve(health, uptime);
  assert(health.output.find("HTTP/1.1", 1) == std::string::npos);

  // Names at and beyond the parser's buffer boundary must not misalign the
  // following attribute, including when the skipped bytes span input reads.
  for (const uint16_t nameLength : {47, 48, 300}) {
    std::string body(attributes, 8);
    auto appendLength = [&body](uint16_t length) {
      body += static_cast<char>(length >> 8);
      body += static_cast<char>(length & 0xff);
    };
    body += '\1';    // Operation attributes.
    body += '\x42';  // Name without language.
    appendLength(nameLength);
    body.append(nameLength, 'x');
    appendLength(1);
    body += 'v';
    body += '\x42';
    appendLength(8);
    body += "job-name";
    appendLength(5);
    body += "after";
    body += '\3';

    MemoryTransport transport(body);
    transport.readLimit = 5;
    IppByteReader reader(transport);
    IppBodyReader input(reader, false, body.size(), body.size());
    IppRequest parsed;
    assert(IppParser::parse(input, parsed));
    assert(std::strcmp(parsed.jobName, "after") == 0);
  }

  // A fragmented print body must finish before the connection is closed.
  std::string raster("UNIRAST", 8);
  raster.append("\0\0\0\1", 4);
  std::string pageHeader(32, '\0');
  pageHeader[0] = 8;   // 8-bit gray.
  pageHeader[15] = 8;  // Width, big-endian.
  pageHeader[19] = 1;  // Height.
  pageHeader[22] = 1;
  pageHeader[23] = 0x2c;  // 300 dpi.
  raster += pageHeader;
  raster.append("\0\7\0", 3);  // One row; repeat eight black pixels.
  std::string printBody(attributes, sizeof(attributes));
  printBody[3] = 2;  // Print-Job.
  printBody += raster;
  MemoryTransport print("POST /ipp/print HTTP/1.1\r\nContent-Type: application/ipp\r\nContent-Length: " +
                        std::to_string(printBody.size()) + "\r\nConnection: keep-alive\r\n\r\n" + printBody);
  print.readLimit = 7;
  PrintedPage page;
  IppPrintService printingService(cfg, page, 8, 1);
  HttpIppConnection printingConnection(printingService, cfg.maxJobBytes);
  printingConnection.serve(print, uptime, false);
  assert(page.complete && page.rows == 1 && printingService.jobsCompleted() == 1);
  assert(print.output.find("Connection: close\r\n") != std::string::npos);
  std::puts("PASS: discovery yields control to the timer; persistent clients and health responses remain valid");
  std::puts("PASS: fragmented print data produces the complete page before closing");
  std::puts("PASS: long attribute names preserve the following IPP attribute");
}
