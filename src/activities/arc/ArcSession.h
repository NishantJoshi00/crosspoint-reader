#pragma once

#include <ArcPack.h>
#include <ArcRuntime.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <atomic>

class ArcSession {
 public:
  enum class Operation : uint8_t { Open, Action, Close, Stop };
  struct Command {
    Operation operation = Operation::Close;
    unsigned action = 0, x = 0, y = 0, level = 0;
    char path[256] = {};
  };

  bool start();
  bool submit(const Command& command);
  void cancel() { cancelRequested.store(true); }
  void shutdown();
  bool busy() const { return pending.load(std::memory_order_acquire); }
  bool succeeded() const { return success; }
  const char* error() const { return message; }
  const arc::Runtime& player() const { return runtime; }

 private:
  static ArcSession* current;
  static void trampoline(void* context);
  static bool poll();
  void run();
  bool open(const Command& command);
  void close();
  bool fail(const char* error);

  QueueHandle_t queue = nullptr;
  TaskHandle_t task = nullptr;
  std::atomic<bool> pending{false};
  std::atomic<bool> stopped{false};
  std::atomic<bool> cancelRequested{false};
  bool success = false;
  char message[192] = {};
  uint32_t started = 0, lastYield = 0;
  arc::Pack pack;
  arc::Runtime runtime;
  void* heap = nullptr;
  esp_partition_mmap_handle_t mapping = 0;
  bool mapped = false;
};
