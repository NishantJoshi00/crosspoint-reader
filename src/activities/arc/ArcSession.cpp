#include "ArcSession.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

ArcSession* ArcSession::current = nullptr;

bool ArcSession::start() {
  if (task) return true;
  stopped.store(false);
  queue = xQueueCreate(1, sizeof(Command));
  if (!queue) return fail("Not enough memory to start the player");
  if (xTaskCreate(trampoline, "ArcPlayer", 20 * 1024, this, 1, &task) != pdPASS) {
    vQueueDelete(queue);
    queue = nullptr;
    return fail("Not enough memory to start the player");
  }
  return true;
}

bool ArcSession::submit(const Command& command) {
  if (!task || busy()) return false;
  cancelRequested.store(false);
  pending.store(true, std::memory_order_release);
  if (xQueueSend(queue, &command, 0) != pdTRUE) {
    pending.store(false, std::memory_order_release);
    return false;
  }
  return true;
}

void ArcSession::shutdown() {
  if (!task) return;
  cancel();
  while (busy()) vTaskDelay(pdMS_TO_TICKS(1));
  Command command;
  command.operation = Operation::Stop;
  submit(command);
  while (!stopped.load(std::memory_order_acquire)) vTaskDelay(pdMS_TO_TICKS(1));
  vQueueDelete(queue);
  queue = nullptr;
  task = nullptr;
}

bool ArcSession::fail(const char* error) {
  std::snprintf(message, sizeof(message), "%s", error);
  return false;
}

void ArcSession::trampoline(void* context) {
  static_cast<ArcSession*>(context)->run();
  vTaskDelete(nullptr);
}

bool ArcSession::poll() {
  if (!current) return true;
  const uint32_t now = millis();
  if (current->cancelRequested.load() || now - current->started > 60000) return false;
  if (now - current->lastYield >= 5) {
    current->lastYield = now;
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return true;
}

void ArcSession::close() {
  runtime.close();
  std::free(heap);
  heap = nullptr;
  if (mapped) esp_partition_munmap(mapping);
  mapped = false;
}

bool ArcSession::open(const Command& command) {
  close();
  if (!Storage.ready()) return fail("Insert the SD card and try again");
  HalFile file = Storage.open(command.path);
  if (!file || file.isDirectory()) return fail("Cannot open this game");
  const size_t size = file.size();
  if (size < arc::Pack::HeaderSize || size > arc::Pack::MaxSize) return fail("Invalid ARC game pack");
  uint8_t header[arc::Pack::HeaderSize];
  if (file.read(header, sizeof(header)) != sizeof(header) || std::memcmp(header, "ARCPACK1", 8) != 0)
    return fail("This file is not an ARC game pack");
  const esp_partition_t* partition =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "arc_cache");
  if (!partition) return fail("Flash the complete firmware to install the ARC cache partition");
  if (size > partition->size) return fail("This game pack is too large");
  if (!file.seek(0)) return fail("Cannot read this game");
  uint8_t buffer[1024];
  uint8_t cached[1024];
  bool identical = true;
  for (size_t offset = 0; offset < size && identical;) {
    if (!poll()) return fail("Loading canceled");
    const size_t count = std::min(sizeof(buffer), size - offset);
    if (file.read(buffer, count) != static_cast<int>(count)) return fail("Cannot read this game; check the SD card");
    identical =
        esp_partition_read(partition, offset, cached, count) == ESP_OK && std::memcmp(buffer, cached, count) == 0;
    offset += count;
  }
  if (!identical) {
    const size_t eraseSize = (size + 4095) & ~size_t(4095);
    if (esp_partition_erase_range(partition, 0, eraseSize) != ESP_OK) return fail("Cannot prepare game storage");
    if (!file.seek(0)) return fail("Cannot read this game");
    for (size_t offset = 0; offset < size;) {
      if (!poll()) return fail("Loading canceled");
      const size_t count = std::min(sizeof(buffer), size - offset);
      if (file.read(buffer, count) != static_cast<int>(count) ||
          esp_partition_write(partition, offset, buffer, count) != ESP_OK)
        return fail("Cannot copy game data; check the SD card");
      offset += count;
    }
  }
  file.close();
  const void* data = nullptr;
  if (esp_partition_mmap(partition, 0, size, ESP_PARTITION_MMAP_DATA, &data, &mapping) != ESP_OK)
    return fail("Cannot map game data into memory");
  mapped = true;
  if (!pack.open(static_cast<const uint8_t*>(data), size)) return fail(pack.error());
  const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  const size_t available = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  if (largest <= 48 * 1024 || available <= 64 * 1024)
    return fail("Not enough memory; restart the device and try again");
  const size_t capacity = std::min(largest - 16 * 1024, available - 32 * 1024) & ~size_t(15);
  heap = heap_caps_malloc(capacity, MALLOC_CAP_8BIT);
  if (!heap) return fail("Not enough memory to load this game");
  LOG_INF("ARC", "Game %s: VM heap=%u, free=%u", pack.gameId(), static_cast<unsigned>(capacity),
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)));
  if (!runtime.open(pack, heap, capacity, command.level < pack.levels() ? command.level : 0))
    return fail(runtime.error());
  return true;
}

void ArcSession::run() {
  current = this;
  arc::Runtime::setStackLimit(16 * 1024);
  arc::Runtime::setPollHook(poll);
  Command command;
  while (xQueueReceive(queue, &command, portMAX_DELAY) == pdTRUE) {
    started = lastYield = millis();
    message[0] = 0;
    switch (command.operation) {
      case Operation::Open:
        success = open(command);
        break;
      case Operation::Action:
        success = runtime.action(command.action, command.x, command.y);
        if (!success) fail(runtime.error());
        break;
      case Operation::Close:
      case Operation::Stop:
        close();
        success = true;
        break;
    }
    if (!success) {
      LOG_ERR("ARC", "%s", message);
      close();
    }
    if (command.operation == Operation::Stop) {
      arc::Runtime::setPollHook(nullptr);
      current = nullptr;
      pending.store(false, std::memory_order_release);
      stopped.store(true, std::memory_order_release);
      return;
    }
    pending.store(false, std::memory_order_release);
  }
}
