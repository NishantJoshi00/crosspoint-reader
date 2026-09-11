#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <set>
#include <vector>

#include "ArcGraphics.h"
#include "ArcPack.h"

namespace {
void put32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = value >> (8 * i);
}
void checksum(std::vector<uint8_t>& bytes) { put32(bytes, 68, arc::Pack::crc32(bytes.data() + 72, bytes.size() - 72)); }
std::vector<uint8_t> fixture() {
  std::vector<uint8_t> bytes(136);
  std::memcpy(bytes.data(), "ARCPACK1", 8);
  bytes[8] = bytes[10] = bytes[16] = bytes[18] = 1;
  put32(bytes, 12, bytes.size());
  std::memcpy(bytes.data() + 20, "ls20-9607627b", 13);
  std::memcpy(bytes.data() + 72, "_arc_boot.mpy", 14);
  put32(bytes, 120, 132);
  put32(bytes, 124, 4);
  bytes[132] = 'M';
  bytes[133] = 6;
  bytes[135] = 31;
  put32(bytes, 128, arc::Pack::crc32(bytes.data() + 132, 4));
  checksum(bytes);
  return bytes;
}
arc::SpriteView sprite(const int8_t* pixels, int rows, int cols) {
  arc::SpriteView result;
  result.pixels = pixels;
  result.rows = rows;
  result.cols = cols;
  result.rowStride = cols;
  return result;
}
}  // namespace

TEST(ArcPack, LoadsBoundedModules) {
  auto bytes = fixture();
  arc::Pack pack;
  ASSERT_TRUE(pack.open(bytes.data(), bytes.size()));
  size_t length;
  EXPECT_EQ(pack.find("./_arc_boot.mpy", length), bytes.data() + 132);
  EXPECT_EQ(length, 4u);
  EXPECT_TRUE(pack.directory("."));
  EXPECT_FALSE(pack.directory("arcengine"));
  EXPECT_EQ(pack.find("missing.mpy", length), nullptr);
  EXPECT_EQ(length, 0u);
}

TEST(ArcPack, RejectsTruncationAndDamage) {
  auto bytes = fixture();
  arc::Pack pack;
  for (size_t size = 0; size < bytes.size(); ++size) EXPECT_FALSE(pack.open(bytes.data(), size));
  bytes.back() ^= 1;
  EXPECT_FALSE(pack.open(bytes.data(), bytes.size()));
  EXPECT_FALSE(pack.open(nullptr, bytes.size()));
}

TEST(ArcPack, RejectsMalformedDirectoryEvenWithCorrectBodyChecksum) {
  for (unsigned mutation = 0; mutation < 9; ++mutation) {
    auto bytes = fixture();
    switch (mutation) {
      case 0:
        put32(bytes, 120, 0xffffffff);
        break;
      case 1:
        put32(bytes, 124, 0xffffffff);
        break;
      case 2:
        put32(bytes, 120, 100);
        break;
      case 3:
        std::memset(bytes.data() + 72, 'a', 48);
        break;
      case 4:
        std::memcpy(bytes.data() + 72, "../bad.mpy", 11);
        break;
      case 5:
        bytes[134] = 1;
        break;  // Native machine code is forbidden.
      case 6:
        bytes[135] = 63;
        break;
      case 7:
        bytes[10] = 2;
        break;
      case 8:
        bytes[18] = 0;
        break;
    }
    checksum(bytes);
    arc::Pack pack;
    EXPECT_FALSE(pack.open(bytes.data(), bytes.size())) << mutation;
  }
}

TEST(ArcGraphics, RotatesThenMirrorsThenScales) {
  const int8_t pixels[] = {1, 2, 3, 4, 5, 6};
  auto s = sprite(pixels, 2, 3);
  s.rotation = 90;
  ASSERT_TRUE(s.valid());
  EXPECT_EQ(s.width(), 2);
  EXPECT_EQ(s.height(), 3);
  EXPECT_EQ(s.sample(0, 0), 4);
  EXPECT_EQ(s.sample(2, 1), 3);
  s.mirrorX = true;
  s.scale = 2;
  EXPECT_EQ(s.width(), 4);
  EXPECT_EQ(s.height(), 6);
  EXPECT_EQ(s.sample(0, 0), 1);
  EXPECT_EQ(s.sample(5, 3), 6);
  s.rotation = 270;
  s.mirrorX = false;
  s.scale = 1;
  EXPECT_EQ(s.sample(0, 0), 3);
  EXPECT_EQ(s.sample(2, 1), 4);
}

TEST(ArcGraphics, DownscaleBreaksTiesTowardHighestColor) {
  const int8_t pixels[] = {1, 2, -1, -1};
  auto s = sprite(pixels, 2, 2);
  s.scale = -1;
  ASSERT_TRUE(s.valid());
  EXPECT_EQ(s.sample(0, 0), 2);
  const int8_t transparent[] = {-1, -1, -1, 5};
  s.pixels = transparent;
  EXPECT_EQ(s.sample(0, 0), -1);
  const int8_t invisible[] = {-2, 0, 0, 0};
  s.pixels = invisible;
  EXPECT_FALSE(s.valid());  // The pinned SDK raises ValueError in bincount.
  s.rows = 3;
  EXPECT_FALSE(s.valid());
}

TEST(ArcGraphics, InvisiblePixelsCollideButDoNotPaint) {
  const int8_t invisible[] = {-2}, transparent[] = {-1}, visible[] = {6};
  auto a = sprite(invisible, 1, 1), b = sprite(visible, 1, 1);
  EXPECT_TRUE(arc::collide(a, b));
  a.pixels = transparent;
  EXPECT_FALSE(arc::collide(a, b));
  a.blocking = b.blocking = 2;
  EXPECT_TRUE(arc::collide(a, b));
  a.collidable = false;
  EXPECT_FALSE(arc::collide(a, b));
  EXPECT_TRUE(arc::collide(a, b, true));
  int8_t frame[] = {4};
  a.pixels = invisible;
  arc::paint(frame, 1, 1, a);
  EXPECT_EQ(frame[0], 4);
}

TEST(ArcGraphics, CameraClipsAndLetterboxes) {
  const int8_t pixels[] = {1, 2, 3, 4};
  auto s = sprite(pixels, 2, 2);
  s.x = -1;
  s.y = -1;
  int8_t raw[2] = {0, 0};
  arc::paint(raw, 2, 1, s);
  EXPECT_EQ(raw[0], 4);
  EXPECT_EQ(raw[1], 0);
  std::array<int8_t, 4096> frame;
  arc::scaleCamera(raw, 2, 1, frame.data(), 5);
  EXPECT_EQ(frame[0], 5);
  EXPECT_EQ(frame[16 * 64], 4);
  EXPECT_EQ(frame[16 * 64 + 32], 0);
  EXPECT_EQ(frame[48 * 64], 5);
}

TEST(ArcGraphics, EveryPalettePatternIsDistinct) {
  std::set<uint64_t> patterns;
  for (unsigned color = 0; color < 16; ++color) {
    uint64_t bits = 0;
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 8; ++x) bits |= uint64_t(arc::ink(color, x, y)) << (y * 8 + x);
    patterns.insert(bits);
  }
  EXPECT_EQ(patterns.size(), 16u);
}
