#include "IppParser.h"

#include <cstring>

#include "IppLog.h"
#include "IppProto.h"

namespace {

bool readU16(IppBodyReader& body, uint16_t& v) {
  uint8_t b[2];
  if (!body.readExact(b, 2)) return false;
  v = static_cast<uint16_t>((b[0] << 8) | b[1]);
  return true;
}

bool readU32(IppBodyReader& body, uint32_t& v) {
  uint8_t b[4];
  if (!body.readExact(b, 4)) return false;
  v = (static_cast<uint32_t>(b[0]) << 24) | (static_cast<uint32_t>(b[1]) << 16) | (static_cast<uint32_t>(b[2]) << 8) |
      static_cast<uint32_t>(b[3]);
  return true;
}

bool skipBytes(IppBodyReader& body, size_t len) {
  uint8_t scratch[64];
  while (len > 0) {
    const size_t chunk = len < sizeof(scratch) ? len : sizeof(scratch);
    if (!body.readExact(scratch, chunk)) return false;
    len -= chunk;
  }
  return true;
}

}  // namespace

bool IppParser::parse(IppBodyReader& body, IppRequest& out, IppRequestObserver* observer) {
  uint8_t hdr[8];
  if (!body.readExact(hdr, 8)) return false;
  out.verMajor = hdr[0];
  out.verMinor = hdr[1];
  out.operationId = static_cast<uint16_t>((hdr[2] << 8) | hdr[3]);
  out.requestId = (static_cast<uint32_t>(hdr[4]) << 24) | (static_cast<uint32_t>(hdr[5]) << 16) |
                  (static_cast<uint32_t>(hdr[6]) << 8) | static_cast<uint32_t>(hdr[7]);
  if (observer) observer->onRequestStarted(out.operationId);

  // Attribute groups: delimiter tags 0x00-0x0F, value tags 0x10+. We track the
  // current attribute name to capture the few we care about, and treat
  // additional-values (empty name) as belonging to the previous attribute.
  char curName[48] = {0};
  uint8_t tagByte;

  while (true) {
    if (!body.readExact(&tagByte, 1)) return false;
    if (tagByte == IppProto::TAG_END_OF_ATTRS) break;
    if (tagByte <= 0x0F) continue;  // begin-attribute-group delimiter

    // attribute-with-one-value: name-length name value-length value
    uint16_t nameLen;
    if (!readU16(body, nameLen)) return false;
    if (nameLen > 0) {
      const size_t keep = nameLen < sizeof(curName) - 1 ? nameLen : sizeof(curName) - 1;
      if (!body.readExact(reinterpret_cast<uint8_t*>(curName), keep)) return false;
      curName[keep] = '\0';
      // skipBytes also handles zero bytes when the complete name fits.
      if (!skipBytes(body, nameLen - keep)) return false;
    }

    uint16_t valLen;
    if (!readU16(body, valLen)) return false;

    if (strcmp(curName, "document-format") == 0 && valLen < sizeof(out.documentFormat)) {
      if (!body.readExact(reinterpret_cast<uint8_t*>(out.documentFormat), valLen)) return false;
      out.documentFormat[valLen] = '\0';
    } else if (strcmp(curName, "job-name") == 0 && valLen < sizeof(out.jobName)) {
      if (!body.readExact(reinterpret_cast<uint8_t*>(out.jobName), valLen)) return false;
      out.jobName[valLen] = '\0';
    } else if (strcmp(curName, "job-id") == 0 && valLen == 4) {
      uint32_t v;
      if (!readU32(body, v)) return false;
      out.jobId = static_cast<int32_t>(v);
    } else {
      if (!skipBytes(body, valLen)) return false;
    }
  }

  out.parseOk = true;
  IPP_LOG_DBG("request op=0x%04x id=%u ver=%u.%u fmt='%s'", out.operationId, static_cast<unsigned>(out.requestId),
              out.verMajor, out.verMinor, out.documentFormat);
  return true;
}
