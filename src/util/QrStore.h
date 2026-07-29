#pragma once

#include <string>
#include <vector>

// SD-backed store for saved QR codes. One file per code under /qrcodes:
// the filename (minus .txt) is the display name, the file content is the
// raw QR payload string. Files may also be dropped in from a computer or
// via WebDAV.
namespace QrStore {

constexpr const char* QR_DIR = "/qrcodes";
constexpr size_t MAX_NAME_LENGTH = 32;
// QR version 40, ECC_LOW, byte mode — mirrors MAX_QR_CAPACITY in QrUtils.
constexpr size_t MAX_PAYLOAD_LENGTH = 2953;

// Fills `names` with the saved code names, sorted alphabetically.
// Returns false when the directory cannot be opened (no codes saved yet).
bool listCodes(std::vector<std::string>& names);

// Loads the payload for `name`. Trailing whitespace/newlines are stripped
// (files written on a computer often end with one).
bool loadPayload(const std::string& name, std::string& payload);

// Writes the payload for `name`, creating /qrcodes if needed.
bool savePayload(const std::string& name, const std::string& payload);

// Keeps alphanumerics, space, hyphen, underscore and dot; trims whitespace;
// truncates to MAX_NAME_LENGTH. Returns "" when nothing survives.
std::string sanitizeName(const std::string& raw);

// Returns `name` if unused, otherwise "name 2", "name 3", ...
std::string uniqueName(const std::string& name);

}  // namespace QrStore
