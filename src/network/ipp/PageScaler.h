#pragma once
#include <cstdint>

#include "PageSink.h"

// Receives arbitrary-size gray pages from RasterDecoder and emits finished
// 1-bit target rows (box downsample + Floyd-Steinberg dither), letterboxed to
// the target page size.
//
// Deliberately holds NO page bitmap: a second full-page copy costs 48 KB, which
// the device does not have once WiFi is up (measured: ~6.8 KB free with one).
// The consumer draws each row straight into the panel framebuffer.
class ScaledPageSink {
 public:
  virtual ~ScaledPageSink() = default;
  virtual bool onScaledPageBegin(uint32_t pageIndex, int boxX, int boxY, int boxW, int boxH) = 0;
  // rowBits: MSB-first, bit set = black, `width` pixels wide, to be drawn at
  // target row `y` starting at column `xOffset`.
  virtual bool onScaledRow(int y, int xOffset, const uint8_t* rowBits, int width) = 0;
  virtual void onScaledPageEnd(bool ok, uint32_t pageIndex) = 0;
};

class PageScaler final : public PageSink {
 public:
  static constexpr int MAX_TARGET_WIDTH = 800;
  static constexpr int MAX_ROW_BYTES = (MAX_TARGET_WIDTH + 7) / 8;

  PageScaler(int outW, int outH, ScaledPageSink& consumer);

  bool onPageBegin(uint32_t widthPx, uint32_t heightPx, uint32_t dpi, uint32_t pageIndex) override;
  bool onRow(const uint8_t* gray, uint32_t widthPx, uint32_t repeatCount) override;
  void onPageEnd(bool ok) override;

 private:
  const int outW;
  const int outH;
  ScaledPageSink& consumer;

  uint32_t srcW = 0, srcH = 0;
  uint32_t srcY = 0;
  uint32_t curPage = 0;
  int boxX = 0, boxY = 0, boxW = 0, boxH = 0;
  int curTargetRow = -1;
  bool active = false;
  bool aborted = false;

  uint32_t sum[MAX_TARGET_WIDTH] = {};
  uint16_t cnt[MAX_TARGET_WIDTH] = {};
  int16_t err[MAX_TARGET_WIDTH + 2] = {};      // FS error carried into the next row
  int16_t nextErr[MAX_TARGET_WIDTH + 2] = {};  // member, not a local: too big for the stack
  uint8_t rowBits[MAX_ROW_BYTES] = {};

  void resetAccumulators();
  void flushTargetRow();
};
