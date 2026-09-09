#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Byte transport abstraction for the IPP server core. The firmware implements
// this over WiFiClient; the host harness over a POSIX socket. Keeping the core
// free of Arduino types is what makes it host-testable (same pattern as
// freeink-sdk/libs/book/FreeInkBook).
class IppTransport {
 public:
  virtual ~IppTransport() = default;
  // Blocking read of up to maxLen bytes. Returns bytes read (>0), 0 on orderly
  // close, <0 on error/timeout.
  virtual int read(uint8_t* buf, size_t maxLen) = 0;
  // Write all len bytes. Returns false on error.
  virtual bool write(const uint8_t* buf, size_t len) = 0;
};

// Small buffered reader over IppTransport: byte-at-a-time parsing without a
// syscall per byte. Buffer is a fixed member — no heap.
class IppByteReader {
  IppTransport& io;
  uint8_t buf[512] = {};
  size_t fill = 0;
  size_t pos = 0;

 public:
  explicit IppByteReader(IppTransport& io) : io(io) {}

  bool hasBufferedData() const { return pos < fill; }

  // Returns -1 on EOF/error, else 0..255.
  int readByte() {
    if (pos >= fill) {
      const int n = io.read(buf, sizeof(buf));
      if (n <= 0) return -1;
      fill = static_cast<size_t>(n);
      pos = 0;
    }
    return buf[pos++];
  }

  bool readExact(uint8_t* out, size_t len) {
    while (len > 0) {
      if (pos < fill) {
        const size_t chunk = (fill - pos < len) ? fill - pos : len;
        memcpy(out, buf + pos, chunk);
        pos += chunk;
        out += chunk;
        len -= chunk;
      } else {
        const int n = io.read(buf, sizeof(buf));
        if (n <= 0) return false;
        fill = static_cast<size_t>(n);
        pos = 0;
      }
    }
    return true;
  }

  bool skipExact(size_t len) {
    uint8_t scratch[64];
    while (len > 0) {
      const size_t chunk = len < sizeof(scratch) ? len : sizeof(scratch);
      if (!readExact(scratch, chunk)) return false;
      len -= chunk;
    }
    return true;
  }

  // Reads a CRLF-terminated line (CR optional), NUL-terminates, strips CRLF.
  // Returns false on EOF or when the line exceeds maxLen-1 bytes.
  bool readLine(char* out, size_t maxLen) {
    size_t n = 0;
    while (n + 1 < maxLen) {
      const int c = readByte();
      if (c < 0) return false;
      if (c == '\n') {
        if (n > 0 && out[n - 1] == '\r') n--;
        out[n] = '\0';
        return true;
      }
      out[n++] = static_cast<char>(c);
    }
    return false;
  }
};

// HTTP body framing over IppByteReader: identity (Content-Length) or chunked
// transfer encoding. Exposes a plain read-bytes interface to the IPP layer and
// enforces a hard cumulative byte cap so a client cannot stream unbounded data
// at the device (the negotiation-plus-enforcement half of job size limits).
class IppBodyReader {
  IppByteReader& in;
  bool chunked;
  uint64_t remaining;  // bytes left in current chunk, or in identity body
  uint64_t consumed = 0;
  uint64_t capBytes;
  bool done = false;
  bool overCap = false;

  bool nextChunkHeader() {
    char line[32];
    if (!in.readLine(line, sizeof(line))) return false;
    if (line[0] == '\0' && !in.readLine(line, sizeof(line))) return false;  // skip blank after prior chunk
    uint64_t size = 0;
    for (const char* p = line; *p; p++) {
      const char c = *p;
      if (c >= '0' && c <= '9')
        size = size * 16 + (c - '0');
      else if (c >= 'a' && c <= 'f')
        size = size * 16 + (c - 'a' + 10);
      else if (c >= 'A' && c <= 'F')
        size = size * 16 + (c - 'A' + 10);
      else
        break;  // chunk extensions — ignore
    }
    remaining = size;
    if (size == 0) {
      done = true;
      in.readLine(line, sizeof(line));  // trailing CRLF (or trailer we ignore)
    }
    return true;
  }

 public:
  IppBodyReader(IppByteReader& in, bool chunked, uint64_t contentLength, uint64_t capBytes)
      : in(in), chunked(chunked), remaining(chunked ? 0 : contentLength), capBytes(capBytes) {
    if (!chunked && contentLength == 0) done = true;
  }

  bool exceededCap() const { return overCap; }
  uint64_t bytesConsumed() const { return consumed; }

  // Returns bytes read (>0), 0 at end of body, <0 on transport error.
  int read(uint8_t* out, size_t maxLen) {
    if (done || overCap) return 0;
    if (chunked && remaining == 0) {
      if (!nextChunkHeader()) return -1;
      if (done) return 0;
    }
    const uint64_t want64 = remaining < maxLen ? remaining : maxLen;
    const size_t want = static_cast<size_t>(want64);
    if (!in.readExact(out, want)) return -1;
    remaining -= want;
    consumed += want;
    if (consumed > capBytes) {
      overCap = true;
      return 0;
    }
    if (!chunked && remaining == 0) done = true;
    return static_cast<int>(want);
  }

  bool readExact(uint8_t* out, size_t len) {
    while (len > 0) {
      const int n = read(out, len);
      if (n <= 0) return false;
      out += n;
      len -= static_cast<size_t>(n);
    }
    return true;
  }

  // Drain the rest of the body so the connection can be reused (keep-alive).
  // Bounded by the cap; returns false if the transport died.
  bool drain() {
    uint8_t scratch[256];
    while (!done && !overCap) {
      if (read(scratch, sizeof(scratch)) < 0) return false;
    }
    return true;
  }
};
