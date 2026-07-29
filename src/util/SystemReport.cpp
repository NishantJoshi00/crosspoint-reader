#include "SystemReport.h"

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Wire.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <nvs.h>

namespace {

// The X3-only peripheral bus (fuel gauge, RTC, IMU) uses the X3_I2C_SDA /
// X3_I2C_SCL / X3_I2C_FREQ pin macros from HalGPIO.h.

const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "power-on";
    case ESP_RST_SW:
      return "software restart";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
      return "task watchdog";
    case ESP_RST_WDT:
      return "other watchdog";
    case ESP_RST_DEEPSLEEP:
      return "deep sleep wake";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "SDIO";
    default:
      return "unknown";
  }
}

const char* i2cDeviceName(uint8_t addr, uint8_t activeTouchAddr) {
  if (addr != 0 && addr == activeTouchAddr) return "touch controller (active)";
  switch (addr) {
    case 0x14:
    case 0x5D:
      return "GT911 touch controller";
    case 0x2E:
      return "CHSC6x touch controller";
    case 0x55:
      return "BQ27220 fuel gauge";
    case 0x68:
      return "DS3231 RTC";
    case 0x6A:
    case 0x6B:
      return "QMI8658 IMU";
    default:
      return "unknown device";
  }
}

bool readReg8(uint8_t addr, uint8_t reg, uint8_t* out) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, static_cast<uint8_t>(1), static_cast<uint8_t>(true)) < 1) return false;
  *out = Wire.read();
  return true;
}

bool readReg16LE(uint8_t addr, uint8_t reg, uint16_t* out) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, static_cast<uint8_t>(2), static_cast<uint8_t>(true)) < 2) {
    while (Wire.available()) Wire.read();
    return false;
  }
  const uint8_t lo = Wire.read();
  const uint8_t hi = Wire.read();
  *out = (static_cast<uint16_t>(hi) << 8) | lo;
  return true;
}

// Read-only identification of known chips found during the sweep. Every read
// here is a plain register read; nothing writes device state.
void identifyDevice(Print& out, uint8_t addr) {
  if (addr == 0x55) {  // BQ27220: state of charge + voltage
    uint16_t soc = 0;
    uint16_t mv = 0;
    if (readReg16LE(addr, 0x2C, &soc) && readReg16LE(addr, 0x08, &mv)) {
      out.printf("      id: SoC %u%%, %u mV\n", soc, mv);
    }
  } else if (addr == 0x68) {  // DS3231: BCD time + temperature
    uint8_t sec = 0;
    uint8_t min = 0;
    uint8_t hour = 0;
    uint8_t tempMsb = 0;
    uint8_t tempLsb = 0;
    if (readReg8(addr, 0x00, &sec) && readReg8(addr, 0x01, &min) && readReg8(addr, 0x02, &hour)) {
      out.printf("      id: time %02x:%02x:%02x (BCD)", hour, min, sec);
      if (readReg8(addr, 0x11, &tempMsb) && readReg8(addr, 0x12, &tempLsb)) {
        out.printf(", temp %d.%02u C", static_cast<int8_t>(tempMsb), (tempLsb >> 6) * 25);
      }
      out.print("\n");
    }
  } else if (addr == 0x6A || addr == 0x6B) {  // QMI8658: WHO_AM_I
    uint8_t who = 0;
    if (readReg8(addr, 0x00, &who)) {
      out.printf("      id: WHO_AM_I=0x%02X (%s)\n", who, who == 0x05 ? "QMI8658 confirmed" : "unexpected");
    }
  }
}

// Sweeps the currently-initialized Wire bus and prints every ACKing address.
void sweepBus(Print& out, uint8_t activeTouchAddr) {
  int found = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) continue;
    found++;
    out.printf("    0x%02X  %s\n", addr, i2cDeviceName(addr, activeTouchAddr));
    identifyDevice(out, addr);
  }
  if (found == 0) {
    out.print("    (no devices ACKed)\n");
  }
}

void writeI2cSection(Print& out) {
  const auto& touch = BoardConfig::ACTIVE.touch;
  const bool touchBusActive =
      touch.controller != BoardConfig::TouchController::None && touch.sda >= 0 && touch.scl >= 0;
  const uint32_t touchFreq = touch.controller == BoardConfig::TouchController::Gt911 ? 400000 : 100000;

  out.print("[i2c]\n");

  if (touchBusActive) {
    // InputManager already initialized Wire on these pins; sweep in place.
    out.printf("  bus: touch (SDA=%d SCL=%d, %lu Hz)\n", touch.sda, touch.scl, static_cast<unsigned long>(touchFreq));
    Wire.setTimeOut(6);
    sweepBus(out, touch.i2cAddress);
  }

  const bool x3BusIsTouchBus = touchBusActive && touch.sda == X3_I2C_SDA && touch.scl == X3_I2C_SCL;
  if (gpio.deviceIsX3() && !x3BusIsTouchBus) {
    out.printf("  bus: X3 peripherals (SDA=%d SCL=%d, %lu Hz)\n", X3_I2C_SDA, X3_I2C_SCL,
               static_cast<unsigned long>(X3_I2C_FREQ));
    Wire.begin(X3_I2C_SDA, X3_I2C_SCL, X3_I2C_FREQ);
    Wire.setTimeOut(6);
    sweepBus(out, 0);
    // Restore the touch bus if we displaced it; otherwise leave this bus up
    // (it is the bus the battery gauge reads use on X3).
    if (touchBusActive) {
      Wire.begin(touch.sda, touch.scl, touchFreq);
      Wire.setTimeOut(4);
    }
  }

  if (!touchBusActive && !gpio.deviceIsX3()) {
    out.print("  (no known I2C bus on this device configuration)\n");
  }
}

void writeReport(Print& out) {
  out.print("CrossPoint hardware report\n");
  out.printf("firmware: %s\n", CROSSPOINT_VERSION);
  out.printf("sdk: %s\n", ESP.getSdkVersion());
  out.printf("reset reason: %s\n", resetReasonName(esp_reset_reason()));
  out.printf("device: Xteink %s\n\n", gpio.deviceIsX3() ? "X3" : "X4");

  out.print("[chip]\n");
  out.printf("  model: %s rev %u, %u core(s) @ %lu MHz\n", ESP.getChipModel(), ESP.getChipRevision(),
             ESP.getChipCores(), static_cast<unsigned long>(ESP.getCpuFreqMHz()));
  const uint64_t mac = ESP.getEfuseMac();
  out.printf("  efuse mac: %02X:%02X:%02X:%02X:%02X:%02X\n\n", static_cast<uint8_t>(mac),
             static_cast<uint8_t>(mac >> 8), static_cast<uint8_t>(mac >> 16), static_cast<uint8_t>(mac >> 24),
             static_cast<uint8_t>(mac >> 32), static_cast<uint8_t>(mac >> 40));

  out.print("[memory]\n");
  out.printf("  heap: %lu total, %lu free, %lu min-ever-free, %lu largest-block\n",  // DRAM only; C3 has no PSRAM
             static_cast<unsigned long>(ESP.getHeapSize()), static_cast<unsigned long>(ESP.getFreeHeap()),
             static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));

  out.print("\n[flash]\n");
  out.printf("  chip: %lu bytes @ %lu Hz\n", static_cast<unsigned long>(ESP.getFlashChipSize()),
             static_cast<unsigned long>(ESP.getFlashChipSpeed()));
  out.printf("  sketch: %lu used, %lu free\n", static_cast<unsigned long>(ESP.getSketchSize()),
             static_cast<unsigned long>(ESP.getFreeSketchSpace()));

  out.print("\n[partitions]\n");
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  while (it != nullptr) {
    const esp_partition_t* p = esp_partition_get(it);
    out.printf("  %-10s type=%d subtype=0x%02x offset=0x%06lx size=%7lu\n", p->label, p->type, p->subtype,
               static_cast<unsigned long>(p->address), static_cast<unsigned long>(p->size));
    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);

  nvs_stats_t nvsStats;
  if (nvs_get_stats(nullptr, &nvsStats) == ESP_OK) {
    out.print("\n[nvs]\n");
    out.printf("  entries: %u used / %u total, %u namespaces\n", nvsStats.used_entries, nvsStats.total_entries,
               nvsStats.namespace_count);
  }

  out.print("\n[battery]\n");
  const BatteryMonitor battery;
  const BatteryMonitor::Status status = battery.readStatus();
  if (!status.supported) {
    out.print("  (no battery telemetry on this board)\n");
  } else {
    if (status.percentageKnown) out.printf("  charge: %u%%\n", status.percentage);
    if (status.millivoltsKnown) out.printf("  voltage: %u mV\n", status.millivolts);
    if (status.chargingKnown) out.printf("  charging: %s\n", status.charging ? "yes" : "no");
    if (status.externalPowerKnown) out.printf("  external power: %s\n", status.externalPower ? "yes" : "no");
  }

  out.print("\n[sdcard]\n");
  const uint64_t total = Storage.totalBytes();
  const uint64_t used = Storage.usedBytes();
  out.printf("  capacity: %llu MB, used: %llu MB\n", total / (1024ULL * 1024ULL), used / (1024ULL * 1024ULL));

  out.print("\n[display]\n");
  out.printf("  panel: 800x480 e-ink, single framebuffer (%u bytes)\n", 800u * 480u / 8u);

  const auto& touch = BoardConfig::ACTIVE.touch;
  out.print("\n[touch]\n");
  if (touch.controller == BoardConfig::TouchController::None) {
    out.print("  none\n");
  } else {
    out.printf("  controller: %s at 0x%02X (SDA=%d SCL=%d IRQ=%d RST=%d)\n",
               touch.controller == BoardConfig::TouchController::Gt911 ? "GT911" : "CHSC6x", touch.i2cAddress,
               touch.sda, touch.scl, touch.irq, touch.reset);
  }

  out.print("\n");
  writeI2cSection(out);
}

}  // namespace

void SystemReport::writeIfMissing() {
  if (Storage.exists(REPORT_PATH)) {
    return;
  }

  const uint32_t startMs = millis();
  HalFile file;
  if (!Storage.openFileForWrite("SYS", REPORT_PATH, file)) {
    LOG_ERR("SYS", "Cannot create %s", REPORT_PATH);
    return;
  }
  writeReport(file);
  LOG_INF("SYS", "Hardware report written to %s in %lu ms", REPORT_PATH,
          static_cast<unsigned long>(millis() - startMs));
}
