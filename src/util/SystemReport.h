#pragma once

// One-shot hardware analysis written to /.sys.txt on the SD card: chip, memory,
// flash, partitions, NVS, battery, SD card, and a deep I2C bus map with chip
// identification. Runs at boot only when the file is absent — delete
// /.sys.txt to trigger a fresh scan on the next boot.
namespace SystemReport {

constexpr const char* REPORT_PATH = "/.sys.txt";

// Writes the report if it does not exist yet. Safe to call once storage and
// GPIO (device detection) are initialized. Never fatal: failures are logged
// and boot continues.
void writeIfMissing();

}  // namespace SystemReport
