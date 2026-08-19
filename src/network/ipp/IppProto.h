#pragma once
#include <cstdint>

// IPP / raster wire-format constants.
// Sources: RFC 8010 (encoding: tag tables 2/4/5/6), RFC 8011 (operation ids
// Appendix, job-state 5.3.7, status codes Appendix B), CUPS raster.h /
// raster-stream.c (sync words, Apple raster header, PWG page header layout).

namespace IppProto {

// --- Delimiter tags (RFC 8010 Table 2) ---
constexpr uint8_t TAG_OPERATION_ATTRS = 0x01;
constexpr uint8_t TAG_JOB_ATTRS = 0x02;
constexpr uint8_t TAG_END_OF_ATTRS = 0x03;
constexpr uint8_t TAG_PRINTER_ATTRS = 0x04;
constexpr uint8_t TAG_UNSUPPORTED_ATTRS = 0x05;

// --- Value tags (RFC 8010 Tables 4/5/6) ---
constexpr uint8_t VTAG_INTEGER = 0x21;
constexpr uint8_t VTAG_BOOLEAN = 0x22;
constexpr uint8_t VTAG_ENUM = 0x23;
constexpr uint8_t VTAG_OCTET_STRING = 0x30;
constexpr uint8_t VTAG_DATE_TIME = 0x31;
constexpr uint8_t VTAG_RESOLUTION = 0x32;  // 9 bytes: xres i32, yres i32, units i8 (3 = dpi)
constexpr uint8_t VTAG_RANGE_OF_INT = 0x33;
constexpr uint8_t VTAG_BEG_COLLECTION = 0x34;
constexpr uint8_t VTAG_END_COLLECTION = 0x37;
constexpr uint8_t VTAG_TEXT = 0x41;  // textWithoutLanguage
constexpr uint8_t VTAG_NAME = 0x42;  // nameWithoutLanguage
constexpr uint8_t VTAG_KEYWORD = 0x44;
constexpr uint8_t VTAG_URI = 0x45;
constexpr uint8_t VTAG_CHARSET = 0x47;
constexpr uint8_t VTAG_NATURAL_LANG = 0x48;
constexpr uint8_t VTAG_MIME_TYPE = 0x49;
constexpr uint8_t VTAG_MEMBER_NAME = 0x4A;

constexpr int RES_UNITS_DPI = 3;  // resolution units value for dots-per-inch

// --- Operation ids (RFC 8011, operation table) ---
constexpr uint16_t OP_PRINT_JOB = 0x0002;
constexpr uint16_t OP_VALIDATE_JOB = 0x0004;
constexpr uint16_t OP_CANCEL_JOB = 0x0008;
constexpr uint16_t OP_GET_JOB_ATTRS = 0x0009;
constexpr uint16_t OP_GET_JOBS = 0x000A;
constexpr uint16_t OP_GET_PRINTER_ATTRS = 0x000B;

// --- Status codes (RFC 8011 Appendix B) ---
constexpr uint16_t STATUS_OK = 0x0000;
constexpr uint16_t STATUS_CLIENT_BAD_REQUEST = 0x0400;
constexpr uint16_t STATUS_CLIENT_NOT_FOUND = 0x0406;
constexpr uint16_t STATUS_CLIENT_ENTITY_TOO_LARGE = 0x0408;
constexpr uint16_t STATUS_CLIENT_FORMAT_NOT_SUPPORTED = 0x040A;
constexpr uint16_t STATUS_CLIENT_FORMAT_ERROR = 0x0411;
constexpr uint16_t STATUS_SERVER_INTERNAL_ERROR = 0x0500;
constexpr uint16_t STATUS_SERVER_OP_NOT_SUPPORTED = 0x0501;

// --- Enum values (RFC 8011 5.3.7 / 5.4.11) ---
constexpr int32_t JOB_STATE_PENDING = 3;
constexpr int32_t JOB_STATE_PROCESSING = 5;
constexpr int32_t JOB_STATE_CANCELED = 7;
constexpr int32_t JOB_STATE_ABORTED = 8;
constexpr int32_t JOB_STATE_COMPLETED = 9;
constexpr int32_t PRINTER_STATE_IDLE = 3;
constexpr int32_t PRINTER_STATE_PROCESSING = 4;

// --- Raster stream sync words (CUPS raster.h) ---
// Apple raster ("URF"): sync 'UNIR' + "AST\0" + u32 BE page count = 12 bytes,
// then per page a 32-byte header. PWG raster: sync "RaS2" once, then per page
// a 1796-byte big-endian serialization of cups_page_header_t.
constexpr uint32_t SYNC_APPLE = 0x554E4952;  // 'UNIR'
constexpr uint32_t SYNC_PWG = 0x52615332;    // 'RaS2'

// Apple raster page header offsets (raster-stream.c cupsRasterReadHeader)
constexpr int URF_HDR_SIZE = 32;
constexpr int URF_OFF_BPP = 0;         // bits per pixel (8 = gray, 24 = sRGB)
constexpr int URF_OFF_COLORSPACE = 1;  // 0=sGray, 1=sRGB, 4=DeviceGray, 5=RGB
constexpr int URF_OFF_DUPLEX = 2;
constexpr int URF_OFF_QUALITY = 3;
constexpr int URF_OFF_WIDTH = 12;   // u32 BE, pixels
constexpr int URF_OFF_HEIGHT = 16;  // u32 BE, pixels
constexpr int URF_OFF_DPI = 20;     // u32 BE

// PWG raster page header offsets (cups_page_header_t serialized BE; strings
// first: MediaClass/MediaColor/MediaType/OutputType = 4 x 64 bytes)
constexpr int PWG_HDR_SIZE = 1796;
constexpr int PWG_OFF_HW_RES_X = 276;
constexpr int PWG_OFF_WIDTH = 372;
constexpr int PWG_OFF_HEIGHT = 376;
constexpr int PWG_OFF_BITS_PER_COLOR = 384;
constexpr int PWG_OFF_BITS_PER_PIXEL = 388;
constexpr int PWG_OFF_BYTES_PER_LINE = 392;
constexpr int PWG_OFF_COLOR_SPACE = 400;
constexpr int PWG_OFF_NUM_COLORS = 420;

// cups_cspace_t values we accept (CUPS raster.h)
constexpr uint32_t CSPACE_W = 0;  // DeviceGray
constexpr uint32_t CSPACE_RGB = 1;
constexpr uint32_t CSPACE_SW = 18;  // sGray
constexpr uint32_t CSPACE_SRGB = 19;

}  // namespace IppProto
