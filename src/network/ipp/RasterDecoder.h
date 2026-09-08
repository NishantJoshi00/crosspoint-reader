#pragma once
#include <cstddef>
#include <cstdint>

#include "IppProto.h"
#include "IppTransport.h"
#include "PageSink.h"

// Streaming decoder for Apple raster (image/urf) and PWG raster
// (image/pwg-raster) document streams, per CUPS raster-stream.c. Both use the
// same modified-PackBits row compression; they differ only in headers.
//
// Memory: one row buffer sized for the widest page we accept (MAX_WIDTH_PX at
// 8-bit gray) plus a small literal-run scratch. Rows are decoded, converted to
// gray, handed to the sink, and discarded — peak RAM is independent of page
// and job size.
class RasterDecoder {
 public:
  // 300dpi Letter is 2550px; anything wider than this is rejected up front.
  static constexpr uint32_t MAX_WIDTH_PX = 2560;

  enum class Result : uint8_t {
    Ok,             // whole document decoded
    FormatError,    // malformed/unsupported stream
    TooWide,        // page wider than MAX_WIDTH_PX
    SinkAbort,      // sink refused the page / row
    TransportError  // body ended early or transport died
  };

  explicit RasterDecoder(PageSink& sink) : sink(sink) {}

  // Consumes the whole document body. maxPages caps how many pages we decode;
  // remaining pages are drained by the caller via the body reader's cap.
  Result decode(IppBodyReader& body, uint32_t maxPages);

 private:
  PageSink& sink;
  uint8_t row[MAX_WIDTH_PX] = {};  // gray output row handed to the sink
  uint8_t literal[128 * 3] = {};   // one literal run: <=128 pixels, <=3 B/px
  // PWG page header prefix holding every field we read (offsets < 424). Member,
  // not a local: 424 bytes would blow the <256B stack-local budget.
  uint8_t pwgHdr[IppProto::PWG_OFF_NUM_COLORS + 4] = {};

  static bool skipRemainder(IppBodyReader& body, size_t len);
  Result decodePage(IppBodyReader& body, uint32_t width, uint32_t height, uint32_t bytesPerPixel, bool whiteIsFF,
                    uint32_t dpi, uint32_t pageIndex);
};
