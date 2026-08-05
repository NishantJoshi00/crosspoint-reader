#pragma once
#include <cstddef>
#include <cstdint>

#include "IppParser.h"
#include "IppTransport.h"
#include "IppWriter.h"
#include "PageScaler.h"
#include "RasterDecoder.h"

// IPP printer object: capability advertisement (the size/format negotiation)
// and operation handling. Transport-agnostic; document data is decoded
// streaming through RasterDecoder -> PageScaler -> ScaledPageSink.
//
// Advertised contract (what clients are told, and what we enforce):
//   - documents: image/urf and image/pwg-raster ONLY (client rasterizes)
//   - color: monochrome only (8-bit gray on the wire)
//   - resolution: 300dpi only
//   - copies: 1
//   - job size: hard cap in bytes; decode aborts past it
struct IppServiceConfig {
  const char* printerName = "CrossPoint X3";
  const char* makeAndModel = "CrossPoint E-Reader Printer";
  const char* printerUri = "ipp://192.168.4.1:631/ipp/print";
  const char* uuidUri = "urn:uuid:8e7a24f2-1f0b-4c9e-9d3a-c0ffee000e01";
  const char* moreInfoUrl = "http://192.168.4.1/";
  uint32_t maxJobBytes = 8 * 1024 * 1024;
  uint32_t maxPages = 1;  // pages decoded per job; the rest are drained
};

class IppPrintService {
 public:
  IppPrintService(const IppServiceConfig& cfg, ScaledPageSink& pageConsumer, int pageW, int pageH)
      : cfg(cfg), scaler(pageW, pageH, pageConsumer), decoder(scaler) {}

  // Handles one parsed request; encodes the IPP response into out. For
  // Print-Job this consumes document data from `body`. upTimeSeconds feeds the
  // printer-up-time attribute. Returns response length (0 = encode overflow).
  size_t handle(const IppRequest& req, IppBodyReader& body, uint8_t* out, size_t outCap, uint32_t upTimeSeconds);

  uint32_t jobsCompleted() const { return lastJobId; }

 private:
  IppServiceConfig cfg;
  PageScaler scaler;
  RasterDecoder decoder;
  uint32_t lastJobId = 0;

  void writeOperationGroup(IppWriter& w);
  void writePrinterAttributes(IppWriter& w, uint32_t upTimeSeconds);
  void writeJobAttributes(IppWriter& w, uint32_t jobId, int32_t jobState);
  uint16_t runPrintJob(const IppRequest& req, IppBodyReader& body);
};
