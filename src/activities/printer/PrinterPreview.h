#pragma once

#include <cstdint>

// At most three previews, at quarter-page milestones and at least two seconds
// apart. Fast jobs and the last quarter go straight to the completed page.
class PrinterPreview {
  uint32_t lastRefresh = 0;
  uint32_t totalRows = 0;
  uint32_t nextPercent = 25;
  bool shown = false;

 public:
  void begin(uint32_t now, uint32_t rows) {
    lastRefresh = now;
    totalRows = rows;
    nextPercent = 25;
    shown = false;
  }

  // Returns the current percentage when a preview is due, otherwise zero.
  uint32_t onRow(uint32_t now, uint32_t completedRows) {
    if (totalRows == 0 || completedRows >= totalRows) return 0;
    const uint32_t percent = static_cast<uint64_t>(completedRows) * 100 / totalRows;
    if (percent < nextPercent || percent > 75 || now - lastRefresh < 2000) return 0;
    nextPercent = (percent / 25 + 1) * 25;
    lastRefresh = now;
    shown = true;
    return percent;
  }

  // Both successful pages and failure screens must consume this flag and
  // replace the provisional panel contents with a stable resync.
  bool finish() {
    const bool needsResync = shown;
    shown = false;
    return needsResync;
  }
};
