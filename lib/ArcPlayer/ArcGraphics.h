#pragma once

#include <cstddef>
#include <cstdint>

namespace arc {

// Palette indices are preserved until the final e-ink presentation. In particular,
// -1 is transparent to collisions; -2 is invisible but participates in collisions.
struct SpriteView {
  const int8_t* pixels = nullptr;
  int rows = 0;
  int cols = 0;
  int rowStride = 0;
  int colStride = 1;
  int rotation = 0;
  int scale = 1;
  bool mirrorX = false;
  bool mirrorY = false;
  int x = 0;
  int y = 0;
  int blocking = 3;
  bool collidable = true;

  int width() const;
  int height() const;
  bool valid() const;
  int8_t sample(int row, int col) const;

 private:
  int8_t transformed(int row, int col) const;
};

bool collide(const SpriteView& a, const SpriteView& b, bool ignoreMode = false);
void paint(int8_t* output, int width, int height, const SpriteView& sprite, int cameraX = 0, int cameraY = 0);
void scaleCamera(const int8_t* view, int width, int height, int8_t* frame, int8_t letterBox);

// Distinct 8x8 monochrome patterns. These are presentation only, never game data.
bool ink(uint8_t color, int x, int y);

}  // namespace arc
