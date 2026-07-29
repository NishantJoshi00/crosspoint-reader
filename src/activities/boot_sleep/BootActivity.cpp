#include "BootActivity.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>

#include <algorithm>
#include <cmath>

namespace {

// Logo geometry (see docs/plans/2026-07-28-boot-logo-animation-design.md):
// equilateral triangles sharing a centroid. An outer fill at R plus an inner
// knockout at R - 2*border gives a uniform border measured perpendicular to
// each edge.
constexpr float LOGO_RADIUS = 52.0f;   // centroid → vertex
constexpr float BORDER_WIDTH = 2.0f;   // white triangle border
constexpr int LOGO_CLEAR_HALF = 58;    // half-size of the square cleared per frame
constexpr int HANDOFF_STEP_DEG = 15;   // per-frame rotation
constexpr int HANDOFF_FINAL_DEG = 60;  // ▽ rotated 60° = △

// Fills an equilateral triangle centered on (cx, cy) with circumradius r,
// rotated angleDeg from the down-pointing base orientation. Scanline fill
// over the three edges; spans drawn with fillRect.
void fillEquilateral(const GfxRenderer& renderer, float cx, float cy, float r, float angleDeg, bool black) {
  float vx[3];
  float vy[3];
  for (int i = 0; i < 3; i++) {
    const float a = (90.0f + 120.0f * i + angleDeg) * static_cast<float>(M_PI) / 180.0f;
    vx[i] = cx + r * cosf(a);
    vy[i] = cy + r * sinf(a);
  }

  const int yStart = static_cast<int>(ceilf(std::min({vy[0], vy[1], vy[2]})));
  const int yEnd = static_cast<int>(floorf(std::max({vy[0], vy[1], vy[2]})));

  for (int y = yStart; y <= yEnd; y++) {
    float xs[3];
    int n = 0;
    for (int i = 0; i < 3; i++) {
      const int j = (i + 1) % 3;
      const float y0 = vy[i];
      const float y1 = vy[j];
      // Half-open edge rule so a scanline through a vertex counts once.
      if ((y >= y0 && y < y1) || (y >= y1 && y < y0)) {
        xs[n++] = vx[i] + (y - y0) * (vx[j] - vx[i]) / (y1 - y0);
      }
    }
    if (n < 2) continue;
    const float xLeft = std::min(xs[0], xs[1]);
    const float xRight = std::max(xs[0], xs[1]);
    const int x0 = static_cast<int>(ceilf(xLeft));
    const int width = static_cast<int>(floorf(xRight)) - x0 + 1;
    if (width > 0) {
      renderer.fillRect(x0, y, width, 1, black);
    }
  }
}

// One logo frame: black triangle at blackAngleDeg beneath the bordered white
// down-pointing triangle. blackAngleDeg 0 leaves the black one fully hidden.
void drawLogoFrame(const GfxRenderer& renderer, int cx, int cy, int blackAngleDeg) {
  renderer.fillRect(cx - LOGO_CLEAR_HALF, cy - LOGO_CLEAR_HALF, LOGO_CLEAR_HALF * 2, LOGO_CLEAR_HALF * 2, false);
  if (blackAngleDeg != 0) {
    fillEquilateral(renderer, cx, cy, LOGO_RADIUS, static_cast<float>(blackAngleDeg), true);
  }
  fillEquilateral(renderer, cx, cy, LOGO_RADIUS, 0.0f, true);
  fillEquilateral(renderer, cx, cy, LOGO_RADIUS - 2.0f * BORDER_WIDTH, 0.0f, false);
}

}  // namespace

void BootActivity::onEnter() {
  Activity::onEnter();

  renderer.clearScreen();
  drawLogoFrame(renderer, renderer.getScreenWidth() / 2, renderer.getScreenHeight() / 2, 0);
  renderer.displayBuffer();
}

void BootActivity::playHandoffAnimation(const GfxRenderer& renderer) {
  const int cx = renderer.getScreenWidth() / 2;
  const int cy = renderer.getScreenHeight() / 2;
  if (gpio.deviceIsX3()) {
    // The X3 driver arms two boot full-syncs in begin(); onEnter()'s splash
    // paint consumed one. Burn the second on the static splash frame so the
    // promotion (full waveform + settle passes, ~2s) lands here instead of
    // freezing the first rotation frame mid-animation.
    drawLogoFrame(renderer, cx, cy, 0);
    renderer.displayBuffer();
  }
  for (int angle = HANDOFF_STEP_DEG; angle <= HANDOFF_FINAL_DEG; angle += HANDOFF_STEP_DEG) {
    drawLogoFrame(renderer, cx, cy, angle);
    // Fast refresh blocks until the panel latches the frame, pacing the steps.
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}
