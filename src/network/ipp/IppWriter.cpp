#include "IppWriter.h"

#include <cstring>

#include "IppProto.h"

void IppWriter::putByte(uint8_t b) {
  if (len + 1 > cap) {
    overflowed = true;
    return;
  }
  buf[len++] = b;
}

void IppWriter::putU16(uint16_t v) {
  putByte(static_cast<uint8_t>(v >> 8));
  putByte(static_cast<uint8_t>(v));
}

void IppWriter::putU32(uint32_t v) {
  putU16(static_cast<uint16_t>(v >> 16));
  putU16(static_cast<uint16_t>(v));
}

void IppWriter::putBytes(const void* p, size_t n) {
  if (len + n > cap) {
    overflowed = true;
    return;
  }
  memcpy(buf + len, p, n);
  len += n;
}

void IppWriter::putHeaderAndName(uint8_t vtag, const char* name) {
  putByte(vtag);
  const size_t nameLen = name ? strlen(name) : 0;
  putU16(static_cast<uint16_t>(nameLen));
  if (nameLen) putBytes(name, nameLen);
}

void IppWriter::begin(uint8_t verMajor, uint8_t verMinor, uint16_t statusCode, uint32_t requestId) {
  putByte(verMajor);
  putByte(verMinor);
  putU16(statusCode);
  putU32(requestId);
}

void IppWriter::beginGroup(uint8_t delimiterTag) { putByte(delimiterTag); }

void IppWriter::end() { putByte(IppProto::TAG_END_OF_ATTRS); }

void IppWriter::addString(uint8_t vtag, const char* name, const char* value) {
  putHeaderAndName(vtag, name);
  const size_t valLen = value ? strlen(value) : 0;
  putU16(static_cast<uint16_t>(valLen));
  if (valLen) putBytes(value, valLen);
}

void IppWriter::addInteger(const char* name, int32_t v) {
  putHeaderAndName(IppProto::VTAG_INTEGER, name);
  putU16(4);
  putU32(static_cast<uint32_t>(v));
}

void IppWriter::addEnum(const char* name, int32_t v, bool additional) {
  putHeaderAndName(IppProto::VTAG_ENUM, additional ? nullptr : name);
  putU16(4);
  putU32(static_cast<uint32_t>(v));
}

void IppWriter::addBoolean(const char* name, bool v) {
  putHeaderAndName(IppProto::VTAG_BOOLEAN, name);
  putU16(1);
  putByte(v ? 1 : 0);
}

void IppWriter::addResolution(const char* name, int32_t dpi, bool additional) {
  // 9 octets: cross-feed res i32, feed res i32, units i8 (RFC 8010 Table 6)
  putHeaderAndName(IppProto::VTAG_RESOLUTION, additional ? nullptr : name);
  putU16(9);
  putU32(static_cast<uint32_t>(dpi));
  putU32(static_cast<uint32_t>(dpi));
  putByte(IppProto::RES_UNITS_DPI);
}

void IppWriter::addRange(const char* name, int32_t lo, int32_t hi) {
  putHeaderAndName(IppProto::VTAG_RANGE_OF_INT, name);
  putU16(8);
  putU32(static_cast<uint32_t>(lo));
  putU32(static_cast<uint32_t>(hi));
}

void IppWriter::beginCollection(const char* name) {
  putHeaderAndName(IppProto::VTAG_BEG_COLLECTION, name);
  putU16(0);
}

void IppWriter::addMemberString(uint8_t vtag, const char* memberName, const char* value) {
  // memberAttrName carries the member's name as its VALUE (RFC 8010 3.1.6).
  putHeaderAndName(IppProto::VTAG_MEMBER_NAME, nullptr);
  const size_t mnLen = strlen(memberName);
  putU16(static_cast<uint16_t>(mnLen));
  putBytes(memberName, mnLen);
  addString(vtag, nullptr, value);
}

void IppWriter::addMemberInteger(const char* memberName, int32_t v) {
  putHeaderAndName(IppProto::VTAG_MEMBER_NAME, nullptr);
  const size_t mnLen = strlen(memberName);
  putU16(static_cast<uint16_t>(mnLen));
  putBytes(memberName, mnLen);
  addInteger(nullptr, v);
}

void IppWriter::beginMemberCollection(const char* memberName) {
  putHeaderAndName(IppProto::VTAG_MEMBER_NAME, nullptr);
  const size_t mnLen = strlen(memberName);
  putU16(static_cast<uint16_t>(mnLen));
  putBytes(memberName, mnLen);
  beginCollection(nullptr);
}

void IppWriter::endCollection() {
  putHeaderAndName(IppProto::VTAG_END_COLLECTION, nullptr);
  putU16(0);
}
