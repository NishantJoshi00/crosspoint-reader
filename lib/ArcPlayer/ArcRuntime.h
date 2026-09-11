#pragma once

#include "ArcPack.h"

namespace arc {

// One VM may be active. All calls must run on the same task, outside rendering.
class Runtime {
 public:
  bool open(const Pack& pack, void* heap, size_t heapSize, unsigned level = 0);
  bool action(unsigned action, unsigned x = 0, unsigned y = 0);
  void close();
  bool active() const { return active_; }
  const char* error() const { return error_; }
  const uint8_t* frame() const { return frame_; }
  unsigned state() const { return state_; }
  unsigned level() const { return level_; }
  unsigned levels() const { return levels_; }
  unsigned actions() const { return actions_; }
  unsigned moves() const { return moves_; }
  size_t heapUsed() const;
  static void setPollHook(bool (*hook)());
  static void setStackLimit(size_t bytes);

 private:
  bool invoke(bool opening, const char* game, unsigned first, unsigned second, unsigned third);
  bool active_ = false;
  uint8_t frame_[4096] = {};
  char error_[192] = {};
  unsigned state_ = 0;
  unsigned level_ = 0;
  unsigned levels_ = 0;
  unsigned actions_ = 0;
  unsigned moves_ = 0;
};

}  // namespace arc
