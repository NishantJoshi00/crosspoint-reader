#include "PageScaler.h"

#include <cstring>

#include "IppLog.h"

PageScaler::PageScaler(uint8_t* outBits, int outW, int outH, ScaledPageSink& consumer)
    : outBits(outBits), outW(outW), outH(outH), outStride((outW + 7) / 8), consumer(consumer) {}

bool PageScaler::onPageBegin(uint32_t widthPx, uint32_t heightPx, uint32_t dpi, uint32_t pageIndex) {
  (void)dpi;
  srcW = widthPx;
  srcH = heightPx;
  srcY = 0;
  curPage = pageIndex;

  // Fit source into target preserving aspect; center the letterbox.
  // 64-bit intermediates: 2550 * 800 overflows int32? (2550*800=2M — fine),
  // but height*width products reach 2550*3300*800 in the mapping math below,
  // so the per-pixel mapping uses precomputed box bounds instead.
  int w = outW;
  int h = static_cast<int>(static_cast<uint64_t>(outW) * srcH / srcW);
  if (h > outH) {
    h = outH;
    w = static_cast<int>(static_cast<uint64_t>(outH) * srcW / srcH);
    if (w > outW) w = outW;
  }
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  boxW = w;
  boxH = h;
  boxX = (outW - w) / 2;
  boxY = (outH - h) / 2;

  memset(outBits, 0x00, static_cast<size_t>(outStride) * outH);  // all white
  memset(err, 0, sizeof(err));
  resetAccumulators();
  curTargetRow = 0;
  active = true;

  IPP_LOG_DBG("scale %ux%u -> box %dx%d at (%d,%d)", srcW, srcH, boxW, boxH, boxX, boxY);
  return consumer.onScaledPageBegin(pageIndex);
}

void PageScaler::resetAccumulators() {
  memset(sum, 0, sizeof(sum));
  memset(cnt, 0, sizeof(cnt));
}

bool PageScaler::onRow(const uint8_t* gray, uint32_t widthPx, uint32_t repeatCount) {
  if (!active || widthPx != srcW) return false;

  for (uint32_t rep = 0; rep < repeatCount && srcY < srcH; rep++, srcY++) {
    // Target row this source row lands in (box filter partitioning).
    const int ty = static_cast<int>(static_cast<uint64_t>(srcY) * boxH / srcH);
    if (ty != curTargetRow) {
      flushTargetRow();
      curTargetRow = ty;
    }
    for (uint32_t sx = 0; sx < srcW; sx++) {
      const int tx = static_cast<int>(static_cast<uint64_t>(sx) * boxW / srcW);
      sum[tx] += gray[sx];
      cnt[tx]++;
    }
  }
  return true;
}

void PageScaler::flushTargetRow() {
  if (curTargetRow < 0 || curTargetRow >= boxH) {
    resetAccumulators();
    return;
  }
  uint8_t* rowOut = outBits + static_cast<size_t>(boxY + curTargetRow) * outStride;

  // Floyd-Steinberg over the box-filtered row. err[] carries spill into the
  // next row; nextErr accumulates it then swaps back.
  memset(nextErr, 0, sizeof(nextErr));

  int carryRight = 0;
  for (int x = 0; x < boxW; x++) {
    const int avg = cnt[x] ? static_cast<int>(sum[x] / cnt[x]) : 255;
    int v = avg + err[x + 1] + carryRight;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    const int out = v < 128 ? 0 : 255;  // 0 = black
    const int e = v - out;
    carryRight = (e * 7) / 16;
    nextErr[x] += static_cast<int16_t>((e * 3) / 16);      // below-left
    nextErr[x + 1] += static_cast<int16_t>((e * 5) / 16);  // below
    nextErr[x + 2] += static_cast<int16_t>(e / 16);        // below-right
    if (out == 0) {
      const int gx = boxX + x;
      rowOut[gx >> 3] |= static_cast<uint8_t>(0x80 >> (gx & 7));
    }
  }
  memcpy(err, nextErr, sizeof(err));
  resetAccumulators();
}

void PageScaler::onPageEnd(bool ok) {
  if (!active) return;
  if (ok) flushTargetRow();
  active = false;
  consumer.onScaledPageEnd(ok, curPage);
}
