#pragma once
#include <cstdint>

// Consumer of decoded raster pages. Rows arrive top-to-bottom as 8-bit gray
// (255 = white), already color-converted by the decoder. repeatCount >= 1 says
// this row occurs that many consecutive times (raster RLE line repeat) — the
// sink applies it without the decoder materializing duplicate rows.
class PageSink {
 public:
  virtual ~PageSink() = default;
  // Return false to abort the job (e.g. page dimensions unacceptable).
  virtual bool onPageBegin(uint32_t widthPx, uint32_t heightPx, uint32_t dpi, uint32_t pageIndex) = 0;
  virtual bool onRow(const uint8_t* gray, uint32_t widthPx, uint32_t repeatCount) = 0;
  virtual void onPageEnd(bool ok) = 0;
};
