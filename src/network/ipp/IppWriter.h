#pragma once
#include <cstddef>
#include <cstdint>

// Builds an IPP response message (RFC 8010 encoding) into a caller-owned
// buffer. All appends bounds-check; overflow() reports truncation so the
// caller can fail the request instead of sending a corrupt message.
class IppWriter {
  uint8_t* buf;
  size_t cap;
  size_t len = 0;
  bool overflowed = false;

  void putByte(uint8_t b);
  void putU16(uint16_t v);
  void putU32(uint32_t v);
  void putBytes(const void* p, size_t n);
  // value-tag + name-length/name (empty name = additional value of a 1setOf)
  void putHeaderAndName(uint8_t vtag, const char* name);

 public:
  IppWriter(uint8_t* buf, size_t cap) : buf(buf), cap(cap) {}

  size_t size() const { return len; }
  bool overflow() const { return overflowed; }
  const uint8_t* data() const { return buf; }

  // Message header: version (echoed from request), status, request-id.
  void begin(uint8_t verMajor, uint8_t verMinor, uint16_t statusCode, uint32_t requestId);
  void beginGroup(uint8_t delimiterTag);
  void end();  // end-of-attributes tag

  // Pass name = nullptr (or "") to append an additional value to the previous
  // attribute (1setOf encoding).
  void addString(uint8_t vtag, const char* name, const char* value);
  void addInteger(const char* name, int32_t v);
  void addEnum(const char* name, int32_t v, bool additional = false);
  void addBoolean(const char* name, bool v);
  void addResolution(const char* name, int32_t dpi, bool additional = false);
  void addRange(const char* name, int32_t lo, int32_t hi);

  // Collections (media-size-supported etc.)
  void beginCollection(const char* name);  // begCollection
  void addMemberString(uint8_t vtag, const char* memberName, const char* value);
  void addMemberInteger(const char* memberName, int32_t v);
  void beginMemberCollection(const char* memberName);  // member whose value is a collection
  void endCollection();
};
