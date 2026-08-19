// Host harness for the IPP printer core (src/network/ipp). Runs the exact
// firmware protocol/decoder sources on a POSIX socket so real macOS/CUPS print
// jobs exercise them; decoded pages land as PBM files for eyeball + pixel
// verification. No mocked inputs.
//
// Usage: ipp_host [port]   (default 6310)
//   lpadmin -p X3TEST -E -v ipp://localhost:6310/ipp/print -m everywhere
//   lp -d X3TEST some.txt
//   open page_1.pbm

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "HttpIppConnection.h"
#include "IppPrintService.h"
#include "PageScaler.h"

namespace {

constexpr int PAGE_W = 480;
constexpr int PAGE_H = 800;
constexpr int PAGE_STRIDE = (PAGE_W + 7) / 8;

time_t startTime = 0;
uint32_t upTimeSeconds() { return static_cast<uint32_t>(time(nullptr) - startTime); }

class SocketTransport final : public IppTransport {
  int fd;

 public:
  explicit SocketTransport(int fd) : fd(fd) {}
  int read(uint8_t* buf, size_t maxLen) override {
    const ssize_t n = ::recv(fd, buf, maxLen, 0);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return -1;  // timeout
    return static_cast<int>(n);
  }
  bool write(const uint8_t* buf, size_t len) override {
    while (len > 0) {
      const ssize_t n = ::send(fd, buf, len, 0);
      if (n <= 0) return false;
      buf += n;
      len -= static_cast<size_t>(n);
    }
    return true;
  }
};

// The firmware draws rows straight into the panel framebuffer; the harness
// accumulates them into a page image so the result can be inspected.
class PbmSink final : public ScaledPageSink {
  uint8_t page[PAGE_STRIDE * PAGE_H];
  int pagesWritten = 0;
  int rowsSeen = 0;

 public:
  bool onScaledPageBegin(uint32_t pageIndex, int boxX, int boxY, int boxW, int boxH) override {
    fprintf(stderr, "[sink] page %u begin, box %dx%d at (%d,%d)\n", pageIndex, boxW, boxH, boxX, boxY);
    memset(page, 0, sizeof(page));
    rowsSeen = 0;
    return true;
  }

  bool onScaledRow(int y, int xOffset, const uint8_t* rowBits, int width) override {
    if (y < 0 || y >= PAGE_H) return false;
    uint8_t* out = page + static_cast<size_t>(y) * PAGE_STRIDE;
    for (int x = 0; x < width; x++) {
      if (rowBits[x >> 3] & (0x80 >> (x & 7))) {
        const int gx = xOffset + x;
        if (gx >= 0 && gx < PAGE_W) out[gx >> 3] |= static_cast<uint8_t>(0x80 >> (gx & 7));
      }
    }
    rowsSeen++;
    return true;
  }

  void onScaledPageEnd(bool ok, uint32_t pageIndex) override {
    if (!ok) {
      fprintf(stderr, "[sink] page %u aborted after %d rows\n", pageIndex, rowsSeen);
      return;
    }
    char name[64];
    snprintf(name, sizeof(name), "page_%d.pbm", ++pagesWritten);
    FILE* f = fopen(name, "wb");
    if (!f) return;
    fprintf(f, "P4\n%d %d\n", PAGE_W, PAGE_H);
    fwrite(page, 1, sizeof(page), f);
    fclose(f);
    fprintf(stderr, "[sink] wrote %s (%d rows)\n", name, rowsSeen);
  }
};

}  // namespace

int main(int argc, char** argv) {
  const int port = argc > 1 ? atoi(argv[1]) : 6310;
  startTime = time(nullptr);

  static PbmSink sink;

  IppServiceConfig cfg;
  char uri[64];
  snprintf(uri, sizeof(uri), "ipp://localhost:%d/ipp/print", port);
  cfg.printerUri = uri;

  static IppPrintService service(cfg, sink, PAGE_W, PAGE_H);
  static HttpIppConnection conn(service, cfg.maxJobBytes);

  const int listenFd = socket(AF_INET, SOCK_STREAM, 0);
  const int one = 1;
  setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(listenFd, 4) != 0) {
    perror("bind/listen");
    return 1;
  }
  fprintf(stderr, "IPP host harness on ipp://localhost:%d/ipp/print\n", port);

  while (true) {
    const int fd = accept(listenFd, nullptr, nullptr);
    if (fd < 0) continue;
    timeval tv{5, 0};  // idle keep-alive connections release the loop
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    fprintf(stderr, "--- connection open\n");
    SocketTransport io(fd);
    conn.serve(io, upTimeSeconds);
    close(fd);
    fprintf(stderr, "--- connection closed\n");
  }
}
