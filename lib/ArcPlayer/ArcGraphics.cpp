// Sprite transforms, collision and camera semantics adapted from ARCEngine.
// Copyright (c) 2026 ARC Prize Foundation. See THIRD_PARTY_NOTICES.md.
#include "ArcGraphics.h"

#include <algorithm>
#include <cstring>

namespace arc {

int SpriteView::width() const {
  const int w = rotation % 180 == 0 ? cols : rows;
  return scale > 0 ? w * scale : w / (1 - scale);
}

int SpriteView::height() const {
  const int h = rotation % 180 == 0 ? rows : cols;
  return scale > 0 ? h * scale : h / (1 - scale);
}

bool SpriteView::valid() const {
  if (!pixels || rows < 1 || cols < 1 || rows > 256 || cols > 256 || scale == 0 || scale < -255 || scale > 64)
    return false;
  if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) return false;
  if (scale > 0) return true;
  const int factor = 1 - scale;
  if (rows % factor || cols % factor) return false;
  // The SDK's bincount rejects -2 in any block that is not mostly -1.
  // Preserve that error instead of silently turning invisible collision cells
  // into transparent cells during a downscale.
  for (int row = 0; row < height(); ++row) {
    for (int col = 0; col < width(); ++col) {
      int negative = 0, nonTransparent = 0;
      bool invisible = false;
      for (int y = 0; y < factor; ++y) {
        for (int xOffset = 0; xOffset < factor; ++xOffset) {
          const int value = transformed(row * factor + y, col * factor + xOffset);
          negative += value < 0;
          nonTransparent += value != -1;
          invisible |= value == -2;
        }
      }
      if (invisible && negative <= nonTransparent) return false;
    }
  }
  return true;
}

int8_t SpriteView::transformed(int row, int col) const {
  const int h = rotation % 180 == 0 ? rows : cols;
  const int w = rotation % 180 == 0 ? cols : rows;
  if (mirrorY) row = h - row - 1;
  if (mirrorX) col = w - col - 1;
  int sourceRow = row;
  int sourceCol = col;
  switch (rotation) {
    case 90:
      sourceRow = rows - col - 1;
      sourceCol = row;
      break;
    case 180:
      sourceRow = rows - row - 1;
      sourceCol = cols - col - 1;
      break;
    case 270:
      sourceRow = col;
      sourceCol = cols - row - 1;
      break;
    default:
      break;
  }
  return pixels[sourceRow * rowStride + sourceCol * colStride];
}

int8_t SpriteView::sample(const int row, const int col) const {
  if (scale > 0) return transformed(row / scale, col / scale);
  // ARCEngine uses the modal opaque palette index, breaking ties toward the
  // highest index. A majority of transparent pixels makes the block transparent.
  const int factor = 1 - scale;
  uint32_t counts[16] = {};
  int transparent = 0;
  for (int y = 0; y < factor; ++y) {
    for (int xOffset = 0; xOffset < factor; ++xOffset) {
      const int value = transformed(row * factor + y, col * factor + xOffset);
      if (value < 0)
        ++transparent;
      else if (value < 16)
        ++counts[value];
    }
  }
  if (transparent > factor * factor - transparent) return -1;
  int winner = 0;
  for (int i = 1; i < 16; ++i) {
    if (counts[i] >= counts[winner]) winner = i;
  }
  return static_cast<int8_t>(winner);
}

bool collide(const SpriteView& a, const SpriteView& b, const bool ignoreMode) {
  if (!ignoreMode && (!a.collidable || !b.collidable || a.blocking == 1 || b.blocking == 1)) return false;
  const int left = std::max(a.x, b.x);
  const int top = std::max(a.y, b.y);
  const int right = std::min(a.x + a.width(), b.x + b.width());
  const int bottom = std::min(a.y + a.height(), b.y + b.height());
  if (left >= right || top >= bottom) return false;
  if (a.blocking != 3 && b.blocking != 3) return true;
  for (int y = top; y < bottom; ++y) {
    for (int x = left; x < right; ++x) {
      if (a.sample(y - a.y, x - a.x) != -1 && b.sample(y - b.y, x - b.x) != -1) return true;
    }
  }
  return false;
}

void paint(int8_t* output, const int width, const int height, const SpriteView& sprite, const int cameraX,
           const int cameraY) {
  const int left = sprite.x - cameraX;
  const int top = sprite.y - cameraY;
  const int bottom = std::min(height, top + sprite.height());
  const int right = std::min(width, left + sprite.width());
  for (int y = std::max(0, top); y < bottom; ++y) {
    for (int x = std::max(0, left); x < right; ++x) {
      const int8_t pixel = sprite.sample(y - top, x - left);
      if (pixel >= 0) output[y * width + x] = pixel;
    }
  }
}

void scaleCamera(const int8_t* view, const int width, const int height, int8_t* frame, const int8_t letterBox) {
  std::memset(frame, letterBox, 4096);
  if (width < 1 || height < 1 || width > 64 || height > 64) return;
  const int scale = std::min(64 / width, 64 / height);
  const int xOffset = (64 - width * scale) / 2;
  const int yOffset = (64 - height * scale) / 2;
  for (int y = 0; y < height * scale; ++y) {
    for (int x = 0; x < width * scale; ++x) {
      frame[(y + yOffset) * 64 + x + xOffset] = view[(y / scale) * width + x / scale];
    }
  }
}

bool ink(const uint8_t color, const int x, const int y) {
  // 0..5 are ARC's white-to-black neutral ramp. Chromatic entries use
  // directional/shape patterns, so equal-luminance colors remain distinct.
  const int px = x & 7;
  const int py = y & 7;
  switch (color) {
    case 0:
      return false;
    case 1:
      return px == 2 && py == 2;
    case 2:
      return (px & 3) == 1 && (py & 3) == 1;
    case 3:
      return (px + py) % 4 == 0;
    case 4:
      return (px + py) % 4 != 0;
    case 5:
      return true;
    case 6:
      return px == 3 || py == 3;
    case 7:
      return px == py || px + py == 7;
    case 8:
      return py < 2;
    case 9:
      return px < 2;
    case 10:
      return (px + py) % 8 < 2;
    case 11:
      return (px - py + 8) % 8 < 2;
    case 12:
      return px > 1 && px < 6 && py > 1 && py < 6;
    case 13:
      return px == 1 || px == 6 || py == 1 || py == 6;
    case 14:
      return (px < 4) == (py < 4);
    case 15:
      return (px == 3 || px == 4) && py > 1 && py < 6;
    default:
      return false;
  }
}

}  // namespace arc
