#pragma once
#include <cstdint>

#include "PageSink.h"

// Receives arbitrary-size gray pages from RasterDecoder and produces a
// letterboxed 1-bit page (bit set = black) in a caller-owned bitmap via box
// downsampling + Floyd-Steinberg dithering. Streaming: holds one target row of
// accumulators, never a full source page.
//
// The consumer is notified when a complete target page is in the bitmap.
class ScaledPageSink {
 public:
  virtual ~ScaledPageSink() = default;
  virtual bool onScaledPageBegin(uint32_t pageIndex) = 0;
  // Bitmap passed to PageScaler's ctor now holds the finished page.
  virtual void onScaledPageEnd(bool ok, uint32_t pageIndex) = 0;
};

class PageScaler final : public PageSink {
 public:
  static constexpr int MAX_TARGET_WIDTH = 800;

  // outBits: ((outW+7)/8) * outH bytes, row-major, MSB first, 1 = black.
  PageScaler(uint8_t* outBits, int outW, int outH, ScaledPageSink& consumer);

  bool onPageBegin(uint32_t widthPx, uint32_t heightPx, uint32_t dpi, uint32_t pageIndex) override;
  bool onRow(const uint8_t* gray, uint32_t widthPx, uint32_t repeatCount) override;
  void onPageEnd(bool ok) override;

 private:
  uint8_t* outBits;
  const int outW;
  const int outH;
  const int outStride;
  ScaledPageSink& consumer;

  // Page-scope state
  uint32_t srcW = 0, srcH = 0;
  uint32_t srcY = 0;
  uint32_t curPage = 0;
  int boxX = 0, boxY = 0, boxW = 0, boxH = 0;  // letterbox placement
  int curTargetRow = -1;                       // target row being accumulated
  bool active = false;

  uint32_t sum[MAX_TARGET_WIDTH];
  uint16_t cnt[MAX_TARGET_WIDTH];
  int16_t err[MAX_TARGET_WIDTH + 2];      // FS error carried to the next row (+2 guard cols)
  int16_t nextErr[MAX_TARGET_WIDTH + 2];  // member, not a local: 1.6KB breaks the stack budget

  void resetAccumulators();
  void flushTargetRow();
};
