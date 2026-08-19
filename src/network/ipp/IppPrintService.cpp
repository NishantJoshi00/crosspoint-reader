#include "IppPrintService.h"

#include <cstring>

#include "IppLog.h"
#include "IppProto.h"

using namespace IppProto;

namespace {
constexpr int ADVERTISED_DPI = 300;
// PWG media sizes in hundredths of millimetres (media-size collections).
constexpr int32_t A5_X = 14800, A5_Y = 21000;
constexpr int32_t LETTER_X = 21590, LETTER_Y = 27940;
}  // namespace

void IppPrintService::writeOperationGroup(IppWriter& w) {
  w.beginGroup(TAG_OPERATION_ATTRS);
  w.addString(VTAG_CHARSET, "attributes-charset", "utf-8");
  w.addString(VTAG_NATURAL_LANG, "attributes-natural-language", "en");
}

void IppPrintService::writeJobAttributes(IppWriter& w, uint32_t jobId, int32_t jobState) {
  w.beginGroup(TAG_JOB_ATTRS);
  w.addInteger("job-id", static_cast<int32_t>(jobId));
  w.addString(VTAG_URI, "job-uri", cfg.printerUri);  // single queue: job addressed via printer URI
  w.addEnum("job-state", jobState);
  w.addString(VTAG_KEYWORD, "job-state-reasons",
              jobState == JOB_STATE_COMPLETED ? "job-completed-successfully" : "none");
}

void IppPrintService::writePrinterAttributes(IppWriter& w, uint32_t upTimeSeconds) {
  w.beginGroup(TAG_PRINTER_ATTRS);

  w.addString(VTAG_URI, "printer-uri-supported", cfg.printerUri);
  w.addString(VTAG_KEYWORD, "uri-security-supported", "none");
  w.addString(VTAG_KEYWORD, "uri-authentication-supported", "none");
  w.addString(VTAG_NAME, "printer-name", cfg.printerName);
  w.addString(VTAG_TEXT, "printer-info", cfg.printerName);
  w.addString(VTAG_TEXT, "printer-make-and-model", cfg.makeAndModel);
  w.addString(VTAG_TEXT, "printer-location", "");
  w.addString(VTAG_URI, "printer-uuid", cfg.uuidUri);
  w.addString(VTAG_URI, "printer-more-info", cfg.moreInfoUrl);

  w.addEnum("printer-state", PRINTER_STATE_IDLE);
  w.addString(VTAG_KEYWORD, "printer-state-reasons", "none");
  w.addBoolean("printer-is-accepting-jobs", true);
  w.addInteger("queued-job-count", 0);
  w.addInteger("printer-up-time", static_cast<int32_t>(upTimeSeconds));

  w.addString(VTAG_KEYWORD, "ipp-versions-supported", "1.1");
  w.addString(VTAG_KEYWORD, nullptr, "2.0");

  w.addEnum("operations-supported", OP_PRINT_JOB);
  w.addEnum(nullptr, OP_VALIDATE_JOB, true);
  w.addEnum(nullptr, OP_CANCEL_JOB, true);
  w.addEnum(nullptr, OP_GET_JOB_ATTRS, true);
  w.addEnum(nullptr, OP_GET_JOBS, true);
  w.addEnum(nullptr, OP_GET_PRINTER_ATTRS, true);

  w.addString(VTAG_CHARSET, "charset-configured", "utf-8");
  w.addString(VTAG_CHARSET, "charset-supported", "utf-8");
  w.addString(VTAG_NATURAL_LANG, "natural-language-configured", "en");
  w.addString(VTAG_NATURAL_LANG, "generated-natural-language-supported", "en");
  w.addString(VTAG_KEYWORD, "pdl-override-supported", "attempted");
  w.addString(VTAG_KEYWORD, "compression-supported", "none");

  // --- The negotiation core: raster only, gray only, one dpi, one copy ---
  w.addString(VTAG_MIME_TYPE, "document-format-default", "image/urf");
  w.addString(VTAG_MIME_TYPE, "document-format-supported", "image/urf");
  w.addString(VTAG_MIME_TYPE, nullptr, "image/pwg-raster");

  w.addString(VTAG_KEYWORD, "print-color-mode-default", "monochrome");
  w.addString(VTAG_KEYWORD, "print-color-mode-supported", "monochrome");
  w.addString(VTAG_KEYWORD, "sides-default", "one-sided");
  w.addString(VTAG_KEYWORD, "sides-supported", "one-sided");
  w.addInteger("copies-default", 1);
  w.addRange("copies-supported", 1, 1);
  w.addEnum("finishings-default", 3);  // 'none'
  w.addEnum("finishings-supported", 3);
  w.addString(VTAG_KEYWORD, "output-bin-default", "face-up");
  w.addString(VTAG_KEYWORD, "output-bin-supported", "face-up");
  w.addEnum("orientation-requested-default", 3);  // portrait
  w.addEnum("orientation-requested-supported", 3);
  w.addEnum("print-quality-default", 4);  // normal
  w.addEnum("print-quality-supported", 4);

  w.addResolution("printer-resolution-default", ADVERTISED_DPI);
  w.addResolution("printer-resolution-supported", ADVERTISED_DPI);

  // Job size ceiling, advertised (job-k-octets) and enforced (body cap).
  w.addRange("job-k-octets-supported", 0, static_cast<int32_t>(cfg.maxJobBytes / 1024));

  // Media: A5 default (closest standard size to the 3:5 e-ink panel), Letter
  // accepted for convenience. Margins zero — we letterbox ourselves.
  w.addString(VTAG_KEYWORD, "media-default", "iso_a5_148x210mm");
  w.addString(VTAG_KEYWORD, "media-supported", "iso_a5_148x210mm");
  w.addString(VTAG_KEYWORD, nullptr, "na_letter_8.5x11in");
  w.addString(VTAG_KEYWORD, "media-ready", "iso_a5_148x210mm");
  w.addString(VTAG_KEYWORD, "media-source-supported", "auto");
  w.addString(VTAG_KEYWORD, "media-type-supported", "stationery");
  w.addInteger("media-top-margin-supported", 0);
  w.addInteger("media-bottom-margin-supported", 0);
  w.addInteger("media-left-margin-supported", 0);
  w.addInteger("media-right-margin-supported", 0);

  w.beginCollection("media-size-supported");
  w.addMemberInteger("x-dimension", A5_X);
  w.addMemberInteger("y-dimension", A5_Y);
  w.endCollection();
  w.beginCollection(nullptr);
  w.addMemberInteger("x-dimension", LETTER_X);
  w.addMemberInteger("y-dimension", LETTER_Y);
  w.endCollection();

  w.beginCollection("media-col-default");
  w.beginMemberCollection("media-size");
  w.addMemberInteger("x-dimension", A5_X);
  w.addMemberInteger("y-dimension", A5_Y);
  w.endCollection();
  w.addMemberInteger("media-top-margin", 0);
  w.addMemberInteger("media-bottom-margin", 0);
  w.addMemberInteger("media-left-margin", 0);
  w.addMemberInteger("media-right-margin", 0);
  w.addMemberString(VTAG_KEYWORD, "media-source", "auto");
  w.addMemberString(VTAG_KEYWORD, "media-type", "stationery");
  w.endCollection();

  // Raster capabilities: PWG 5102.4 attributes + Apple URF keyword set.
  w.addString(VTAG_KEYWORD, "pwg-raster-document-type-supported", "sgray_8");
  w.addResolution("pwg-raster-document-resolution-supported", ADVERTISED_DPI);
  w.addString(VTAG_KEYWORD, "pwg-raster-document-sheet-back", "normal");

  w.addString(VTAG_KEYWORD, "urf-supported", "V1.4");
  w.addString(VTAG_KEYWORD, nullptr, "W8");
  w.addString(VTAG_KEYWORD, nullptr, "SRGB24");
  w.addString(VTAG_KEYWORD, nullptr, "CP1");
  w.addString(VTAG_KEYWORD, nullptr, "RS300");
  w.addString(VTAG_KEYWORD, nullptr, "DM1");
}

uint16_t IppPrintService::runPrintJob(const IppRequest& req, IppBodyReader& body) {
  const char* fmt = req.documentFormat;
  const bool known = fmt[0] == '\0' || strcmp(fmt, "image/urf") == 0 || strcmp(fmt, "image/pwg-raster") == 0 ||
                     strcmp(fmt, "application/octet-stream") == 0;
  if (!known) {
    IPP_LOG_ERR("rejecting document-format '%s'", fmt);
    return STATUS_CLIENT_FORMAT_NOT_SUPPORTED;
  }

  const RasterDecoder::Result r = decoder.decode(body, cfg.maxPages);
  if (body.exceededCap()) {
    IPP_LOG_ERR("job exceeded byte cap (%u bytes)", static_cast<unsigned>(cfg.maxJobBytes));
    return STATUS_CLIENT_ENTITY_TOO_LARGE;
  }
  switch (r) {
    case RasterDecoder::Result::Ok:
      return STATUS_OK;
    case RasterDecoder::Result::TooWide:
    case RasterDecoder::Result::FormatError:
      return STATUS_CLIENT_FORMAT_ERROR;
    case RasterDecoder::Result::SinkAbort:
      return STATUS_SERVER_INTERNAL_ERROR;
    case RasterDecoder::Result::TransportError:
    default:
      return STATUS_CLIENT_BAD_REQUEST;
  }
}

size_t IppPrintService::handle(const IppRequest& req, IppBodyReader& body, uint8_t* out, size_t outCap,
                               uint32_t upTimeSeconds) {
  IppWriter w(out, outCap);
  // Echo the client's version (RFC 8010: response uses the request's version).
  const uint8_t vMaj = req.verMajor;
  const uint8_t vMin = req.verMinor;

  switch (req.operationId) {
    case OP_GET_PRINTER_ATTRS:
      w.begin(vMaj, vMin, STATUS_OK, req.requestId);
      writeOperationGroup(w);
      writePrinterAttributes(w, upTimeSeconds);
      break;

    case OP_VALIDATE_JOB:
      w.begin(vMaj, vMin, STATUS_OK, req.requestId);
      writeOperationGroup(w);
      break;

    case OP_PRINT_JOB: {
      const uint16_t status = runPrintJob(req, body);
      w.begin(vMaj, vMin, status, req.requestId);
      writeOperationGroup(w);
      if (status == STATUS_OK) {
        lastJobId++;
        writeJobAttributes(w, lastJobId, JOB_STATE_COMPLETED);
      }
      break;
    }

    case OP_GET_JOBS:
      // Synchronous printer: no queued jobs, ever.
      w.begin(vMaj, vMin, STATUS_OK, req.requestId);
      writeOperationGroup(w);
      break;

    case OP_GET_JOB_ATTRS:
      if (lastJobId > 0 && (req.jobId < 0 || static_cast<uint32_t>(req.jobId) == lastJobId)) {
        w.begin(vMaj, vMin, STATUS_OK, req.requestId);
        writeOperationGroup(w);
        writeJobAttributes(w, lastJobId, JOB_STATE_COMPLETED);
      } else {
        w.begin(vMaj, vMin, STATUS_CLIENT_NOT_FOUND, req.requestId);
        writeOperationGroup(w);
      }
      break;

    case OP_CANCEL_JOB:
      // Jobs complete within the Print-Job request; nothing is cancellable.
      w.begin(vMaj, vMin, req.jobId >= 0 ? STATUS_OK : STATUS_CLIENT_NOT_FOUND, req.requestId);
      writeOperationGroup(w);
      break;

    default:
      IPP_LOG_ERR("unsupported operation 0x%04x", req.operationId);
      w.begin(vMaj, vMin, STATUS_SERVER_OP_NOT_SUPPORTED, req.requestId);
      writeOperationGroup(w);
      break;
  }

  w.end();
  if (w.overflow()) {
    IPP_LOG_ERR("response overflow (cap %u)", static_cast<unsigned>(outCap));
    return 0;
  }
  return w.size();
}
