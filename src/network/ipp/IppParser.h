#pragma once
#include <cstdint>

#include "IppTransport.h"

// Parses an IPP request's header and attribute groups from the body stream,
// stopping right after the end-of-attributes tag — document data (if any)
// stays in the body reader for the raster decoder. Extracts only the handful
// of attributes we act on; everything else is validated and skipped.
struct IppRequest {
  uint8_t verMajor = 1;
  uint8_t verMinor = 1;
  uint16_t operationId = 0;
  uint32_t requestId = 0;
  char documentFormat[48] = {0};
  char jobName[64] = {0};
  int32_t jobId = -1;
  bool parseOk = false;
};

class IppParser {
 public:
  // Returns false on malformed stream / transport error. On success `out` is
  // filled and `body` is positioned at the document data.
  static bool parse(IppBodyReader& body, IppRequest& out);
};
