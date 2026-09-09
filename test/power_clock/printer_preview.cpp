#include <cassert>
#include <cstdio>
#include <initializer_list>

#include "activities/printer/PrinterPreview.h"

int main() {
  PrinterPreview preview;
  preview.begin(0, 100);
  assert(preview.onRow(1999, 25) == 0);
  assert(preview.onRow(2000, 25) == 25);
  assert(preview.onRow(3000, 50) == 0);
  assert(preview.onRow(4000, 50) == 50);
  assert(preview.onRow(6000, 75) == 75);
  assert(preview.onRow(8000, 90) == 0);
  assert(preview.onRow(10000, 100) == 0);
  assert(preview.finish());  // Completion must request the stable resync.
  assert(!preview.finish());

  // A slow start must not trigger a burst of catch-up refreshes.
  preview.begin(0, 100);
  assert(preview.onRow(4000, 65) == 65);
  assert(preview.onRow(6000, 66) == 0);
  assert(preview.onRow(7000, 75) == 75);
  assert(preview.finish());

  // Abort uses the same resync requirement as success. The next short print
  // starts fresh and must not inherit previews or a pending refresh flag.
  preview.begin(0, 100);
  assert(preview.onRow(2500, 30) == 30);
  assert(preview.finish());
  preview.begin(3000, 100);
  assert(preview.onRow(3500, 50) == 0);
  assert(preview.onRow(4000, 100) == 0);
  assert(!preview.finish());
  preview.begin(0, 0);
  assert(preview.onRow(2000, 1) == 0 && !preview.finish());

  // Check the refresh budget and spacing across row counts, transfer speeds,
  // skipped rows, and a wrapping millisecond counter.
  for (const uint32_t rows : {1u, 2u, 3u, 4u, 100u, 792u}) {
    for (const uint32_t duration : {0u, 250u, 1000u, 1999u, 4000u, 8000u, 120000u}) {
      for (const uint32_t start : {0u, UINT32_MAX - 1000}) {
        preview.begin(start, rows);
        uint32_t lastTime = start;
        uint32_t lastPercent = 0;
        int count = 0;
        for (uint32_t row = 1; row <= rows; ++row) {
          const uint32_t now = start + duration * row / rows;
          const uint32_t percent = preview.onRow(now, row);
          if (percent == 0) continue;
          assert(percent >= 25 && percent <= 75 && percent > lastPercent);
          assert(now - lastTime >= 2000);
          lastTime = now;
          lastPercent = percent;
          ++count;
        }
        assert(count <= 3);
        if (duration < 2000) assert(count == 0);
        assert(preview.finish() == (count > 0));
      }
    }
  }
  std::puts("PASS: progressive previews respect milestones, spacing, refresh budget, completion and cancellation");
}
