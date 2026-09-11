#pragma once

#include <cstddef>
#include <cstdint>

namespace arc {

// All integers in the pack are little-endian. No native structs are serialized.
class Pack {
 public:
  static constexpr size_t HeaderSize = 72;
  static constexpr size_t EntrySize = 60;
  static constexpr size_t MaxSize = 3 * 1024 * 1024;
  static constexpr uint16_t Abi = 1;

  bool open(const uint8_t* bytes, size_t size);
  const uint8_t* find(const char* name, size_t& size) const;
  bool directory(const char* name) const;
  const char* gameId() const { return gameId_; }
  uint16_t levels() const { return levels_; }
  const char* error() const { return error_; }
  static uint32_t crc32(const uint8_t* data, size_t size);

 private:
  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
  uint16_t entries_ = 0;
  uint16_t levels_ = 0;
  char gameId_[17] = {};
  const char* error_ = "No game loaded";
};

}  // namespace arc
