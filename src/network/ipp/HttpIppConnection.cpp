#include "HttpIppConnection.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "IppLog.h"
#include "IppParser.h"

namespace {

// Case-insensitive prefix match for header names.
bool headerIs(const char* line, const char* name) {
  while (*name) {
    const char a = *line >= 'A' && *line <= 'Z' ? *line + 32 : *line;
    const char b = *name >= 'A' && *name <= 'Z' ? *name + 32 : *name;
    if (a != b) return false;
    line++;
    name++;
  }
  return *line == ':';
}

const char* headerValue(const char* line) {
  const char* p = strchr(line, ':');
  if (!p) return "";
  p++;
  while (*p == ' ' || *p == '\t') p++;
  return p;
}

bool valueContains(const char* value, const char* token) {
  // Case-insensitive substring search, adequate for header token tests.
  const size_t tLen = strlen(token);
  for (const char* p = value; *p; p++) {
    size_t i = 0;
    while (i < tLen) {
      const char a = p[i] >= 'A' && p[i] <= 'Z' ? p[i] + 32 : p[i];
      const char b = token[i] >= 'A' && token[i] <= 'Z' ? token[i] + 32 : token[i];
      if (a != b) break;
      i++;
    }
    if (i == tLen) return true;
  }
  return false;
}

}  // namespace

void HttpIppConnection::serve(IppTransport& io, uint32_t (*upTime)()) {
  IppByteReader in(io);
  bool keepAlive = true;
  while (keepAlive) {
    if (!handleOne(io, in, upTime, keepAlive)) break;
  }
}

bool HttpIppConnection::sendSimple(IppTransport& io, const char* status, const char* body) {
  char hdr[256];
  const size_t bodyLen = strlen(body);
  const int n = snprintf(hdr, sizeof(hdr),
                         "HTTP/1.1 %s\r\nContent-Type: text/plain\r\nContent-Length: %u\r\n"
                         "Connection: close\r\n\r\n",
                         status, static_cast<unsigned>(bodyLen));
  if (n <= 0) return false;
  return io.write(reinterpret_cast<const uint8_t*>(hdr), static_cast<size_t>(n)) &&
         io.write(reinterpret_cast<const uint8_t*>(body), bodyLen);
}

bool HttpIppConnection::sendIppResponse(IppTransport& io, size_t ippLen, bool keepAlive) {
  char hdr[192];
  const int n = snprintf(hdr, sizeof(hdr),
                         "HTTP/1.1 200 OK\r\nContent-Type: application/ipp\r\nContent-Length: %u\r\n"
                         "Connection: %s\r\n\r\n",
                         static_cast<unsigned>(ippLen), keepAlive ? "keep-alive" : "close");
  if (n <= 0) return false;
  return io.write(reinterpret_cast<const uint8_t*>(hdr), static_cast<size_t>(n)) && io.write(respBuf, ippLen);
}

bool HttpIppConnection::handleOne(IppTransport& io, IppByteReader& in, uint32_t (*upTime)(), bool& keepAlive) {
  if (!in.readLine(line, sizeof(line))) return false;  // idle close or timeout
  if (line[0] == '\0') return true;                    // stray blank line between requests

  const bool isPost = strncmp(line, "POST ", 5) == 0;
  const bool isGet = strncmp(line, "GET ", 4) == 0;
  IPP_LOG_DBG("http: %s", line);

  // Headers
  uint64_t contentLength = 0;
  bool chunked = false;
  bool expectContinue = false;
  bool isIpp = false;
  keepAlive = true;
  while (true) {
    if (!in.readLine(line, sizeof(line))) return false;
    if (line[0] == '\0') break;
    if (headerIs(line, "Content-Length")) {
      contentLength = strtoull(headerValue(line), nullptr, 10);
    } else if (headerIs(line, "Transfer-Encoding")) {
      chunked = valueContains(headerValue(line), "chunked");
    } else if (headerIs(line, "Expect")) {
      expectContinue = valueContains(headerValue(line), "100-continue");
    } else if (headerIs(line, "Content-Type")) {
      isIpp = valueContains(headerValue(line), "application/ipp");
    } else if (headerIs(line, "Connection")) {
      if (valueContains(headerValue(line), "close")) keepAlive = false;
    }
  }

  if (isGet) {
    // Health probe convenience; not part of the IPP surface.
    sendSimple(io, "200 OK", "CrossPoint X3 IPP printer\n");
    return keepAlive;
  }
  if (!isPost || !isIpp) {
    sendSimple(io, "404 Not Found", "IPP endpoint only\n");
    return false;
  }

  if (expectContinue) {
    static const char kContinue[] = "HTTP/1.1 100 Continue\r\n\r\n";
    if (!io.write(reinterpret_cast<const uint8_t*>(kContinue), sizeof(kContinue) - 1)) return false;
  }

  // The cap covers IPP attributes + document data; +64KB headroom over the
  // advertised job cap so a maximal valid job's attributes still fit.
  IppBodyReader body(in, chunked, contentLength, static_cast<uint64_t>(maxJobBytes) + 64 * 1024);

  IppRequest req;
  if (!IppParser::parse(body, req)) {
    sendSimple(io, "400 Bad Request", "malformed IPP\n");
    return false;
  }

  const size_t ippLen = service.handle(req, body, respBuf, sizeof(respBuf), upTime());
  if (ippLen == 0) {
    sendSimple(io, "500 Internal Server Error", "response encode failure\n");
    return false;
  }

  // Leftover body (error paths, extra pages) must be consumed for keep-alive;
  // if the cap tripped we hard-close instead of reading an unbounded stream.
  if (!body.drain() || body.exceededCap()) keepAlive = false;

  if (!sendIppResponse(io, ippLen, keepAlive)) return false;
  return keepAlive;
}
