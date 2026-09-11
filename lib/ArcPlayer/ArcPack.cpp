#include "ArcPack.h"

#include <cstring>

namespace arc {
namespace {
uint16_t u16(const uint8_t* p) { return p[0] | (uint16_t(p[1]) << 8); }
uint32_t u32(const uint8_t* p) { return u16(p) | (uint32_t(u16(p + 2)) << 16); }
bool terminated(const uint8_t* p, size_t n) { return std::memchr(p, 0, n) != nullptr; }
}  // namespace

uint32_t Pack::crc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xffffffff;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320 & (0u - (crc & 1)));
  }
  return ~crc;
}

bool Pack::open(const uint8_t* bytes, const size_t size) {
  data_ = nullptr;
  entries_ = 0;
  error_ = "Invalid ARC pack";
  if (!bytes || size < HeaderSize || size > MaxSize || std::memcmp(bytes, "ARCPACK1", 8) != 0) return false;
  if (u16(bytes + 8) != 1 || u16(bytes + 10) != Abi) {
    error_ = "This pack needs a different player version";
    return false;
  }
  if (u32(bytes + 12) != size || !terminated(bytes + 20, 16) || bytes[20] == 0) return false;
  const uint16_t count = u16(bytes + 16);
  if (count == 0 || count > 64 || u16(bytes + 18) == 0 || HeaderSize + count * EntrySize > size) return false;
  if (crc32(bytes + HeaderSize, size - HeaderSize) != u32(bytes + 68)) {
    error_ = "Game pack is damaged; copy it again";
    return false;
  }
  size_t end = HeaderSize + count * EntrySize;
  for (unsigned i = 0; i < count; ++i) {
    const auto* entry = bytes + HeaderSize + i * EntrySize;
    if (!terminated(entry, 48) || entry[0] == 0) return false;
    const char* name = reinterpret_cast<const char*>(entry);
    if (name[0] == '/' || std::strstr(name, "..") || std::strchr(name, '\\')) return false;
    const size_t offset = u32(entry + 48), length = u32(entry + 52);
    if (offset < end || offset > size || length < 4 || length > size - offset) return false;
    if (bytes[offset] != 'M' || bytes[offset + 1] != 6 || bytes[offset + 2] != 0 || bytes[offset + 3] > 31)
      return false;
    if (crc32(bytes + offset, length) != u32(entry + 56)) return false;
    for (unsigned j = 0; j < i; ++j) {
      if (std::strcmp(name, reinterpret_cast<const char*>(bytes + HeaderSize + j * EntrySize)) == 0) return false;
    }
    end = offset + length;
  }
  data_ = bytes;
  size_ = size;
  entries_ = count;
  levels_ = u16(bytes + 18);
  std::memcpy(gameId_, bytes + 20, 16);
  gameId_[16] = 0;
  size_t ignored;
  if (!find("_arc_boot.mpy", ignored)) {
    data_ = nullptr;
    return false;
  }
  error_ = "";
  return true;
}

const uint8_t* Pack::find(const char* name, size_t& size) const {
  size = 0;
  if (!data_) return nullptr;
  if (name[0] == '.' && name[1] == '/') name += 2;
  for (unsigned i = 0; i < entries_; ++i) {
    const auto* entry = data_ + HeaderSize + i * EntrySize;
    if (std::strcmp(name, reinterpret_cast<const char*>(entry)) == 0) {
      size = u32(entry + 52);
      return data_ + u32(entry + 48);
    }
  }
  return nullptr;
}

bool Pack::directory(const char* name) const {
  if (!data_) return false;
  if (!*name || std::strcmp(name, ".") == 0) return true;
  if (name[0] == '.' && name[1] == '/') name += 2;
  const size_t length = std::strlen(name);
  if (length >= 47) return false;
  for (unsigned i = 0; i < entries_; ++i) {
    const auto* entry = data_ + HeaderSize + i * EntrySize;
    if (std::strncmp(name, reinterpret_cast<const char*>(entry), length) == 0 && entry[length] == '/') return true;
  }
  return false;
}
}  // namespace arc
