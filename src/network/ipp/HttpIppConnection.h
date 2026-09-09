#pragma once
#include <cstddef>
#include <cstdint>

#include "IppPrintService.h"
#include "IppTransport.h"

// Minimal HTTP/1.1 server loop for one client connection carrying IPP.
// Handles POST application/ipp (identity or chunked framing, Expect:
// 100-continue), keep-alive, and answers anything else with a terse error.
// Arduino's WebServer buffers whole request bodies in RAM, which is exactly
// what we cannot afford — this parses the stream in place instead.
class HttpIppConnection {
 public:
  static constexpr size_t RESPONSE_CAP = 4096;

  HttpIppConnection(IppPrintService& service, uint32_t maxJobBytes, IppRequestObserver* observer = nullptr)
      : service(service), maxJobBytes(maxJobBytes), observer(observer) {}

  // Serves requests on this transport until the peer closes, errors, or sends
  // Connection: close. upTimeSeconds is sampled per-request via the callback.
  // Set allowKeepAlive=false for a timed foreground service so each completed
  // request returns control to input, countdown rendering, and sleep checks.
  void serve(IppTransport& io, uint32_t (*upTime)(), bool allowKeepAlive = true);

  // One request per turn. Retain `in` with its transport between calls: the
  // buffer can contain the start of the next pipelined request. The caller
  // waits for buffered/socket data without blocking on idle connections.
  // Returns true when this connection can be reused.
  bool serveOne(IppTransport& io, IppByteReader& in, uint32_t (*upTime)());

 private:
  IppPrintService& service;
  uint32_t maxJobBytes;
  IppRequestObserver* observer;
  uint8_t respBuf[RESPONSE_CAP] = {};
  char line[512] = {};

  bool handleOne(IppTransport& io, IppByteReader& in, uint32_t (*upTime)(), bool& keepAlive, bool allowKeepAlive);
  bool sendSimple(IppTransport& io, const char* status, const char* body);
  bool sendIppResponse(IppTransport& io, size_t ippLen, bool keepAlive);
};
