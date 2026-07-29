#include "QrStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace {

constexpr const char* EXTENSION = ".txt";
constexpr size_t EXTENSION_LEN = 4;

std::string buildPath(const std::string& name) {
  std::string path;
  path.reserve(strlen(QrStore::QR_DIR) + 1 + name.size() + EXTENSION_LEN);
  path = QrStore::QR_DIR;
  path += '/';
  path += name;
  path += EXTENSION;
  return path;
}

}  // namespace

bool QrStore::listCodes(std::vector<std::string>& names) {
  names.clear();

  auto dir = Storage.open(QR_DIR);
  if (!dir || !dir.isDirectory()) {
    return false;
  }

  names.reserve(16);
  char fileName[MAX_NAME_LENGTH + EXTENSION_LEN + 1];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (file.isDirectory()) continue;
    // getName returns 0 when the name does not fit the buffer — skips names
    // longer than MAX_NAME_LENGTH, which the create flow never produces.
    if (file.getName(fileName, sizeof(fileName)) == 0) continue;
    const size_t len = strlen(fileName);
    if (fileName[0] == '.' || len <= EXTENSION_LEN) continue;
    if (strcasecmp(fileName + len - EXTENSION_LEN, EXTENSION) != 0) continue;
    names.emplace_back(fileName, len - EXTENSION_LEN);
  }

  std::sort(names.begin(), names.end());
  return true;
}

bool QrStore::loadPayload(const std::string& name, std::string& payload) {
  payload.clear();

  HalFile file;
  if (!Storage.openFileForRead("QRS", buildPath(name), file)) {
    return false;
  }

  const size_t fileSize = file.size();
  const size_t toRead = std::min(fileSize, MAX_PAYLOAD_LENGTH);
  payload.resize(toRead);
  const int bytesRead = file.read(payload.data(), toRead);
  if (bytesRead < 0) {
    LOG_ERR("QRS", "Read failed: %s", name.c_str());
    payload.clear();
    return false;
  }
  payload.resize(static_cast<size_t>(bytesRead));

  while (!payload.empty() && (payload.back() == '\n' || payload.back() == '\r' || payload.back() == ' ')) {
    payload.pop_back();
  }
  return !payload.empty();
}

bool QrStore::savePayload(const std::string& name, const std::string& payload) {
  if (name.empty() || payload.empty() || payload.size() > MAX_PAYLOAD_LENGTH) {
    return false;
  }

  if (!Storage.ensureDirectoryExists(QR_DIR)) {
    LOG_ERR("QRS", "Cannot create %s", QR_DIR);
    return false;
  }

  HalFile file;
  if (!Storage.openFileForWrite("QRS", buildPath(name), file)) {
    return false;
  }
  const size_t written = file.write(payload.data(), payload.size());
  if (written != payload.size()) {
    LOG_ERR("QRS", "Short write: %s", name.c_str());
    return false;
  }
  return true;
}

std::string QrStore::sanitizeName(const std::string& raw) {
  std::string name;
  name.reserve(std::min(raw.size(), MAX_NAME_LENGTH));
  for (const char c : raw) {
    if (name.size() >= MAX_NAME_LENGTH) break;
    if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_' || c == '.') {
      name += c;
    }
  }
  while (!name.empty() && name.front() == ' ') name.erase(name.begin());
  while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
  return name;
}

std::string QrStore::uniqueName(const std::string& name) {
  if (!Storage.exists(buildPath(name).c_str())) {
    return name;
  }
  for (int i = 2; i < 100; i++) {
    char suffix[8];
    snprintf(suffix, sizeof(suffix), " %d", i);
    std::string candidate = name.substr(0, MAX_NAME_LENGTH - strlen(suffix)) + suffix;
    if (!Storage.exists(buildPath(candidate).c_str())) {
      return candidate;
    }
  }
  return name;  // give up and overwrite
}
