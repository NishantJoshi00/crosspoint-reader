#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

#include "ArcPack.h"
#include "ArcRuntime.h"

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream input(argv[1], std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  arc::Pack pack;
  if (!pack.open(bytes.data(), bytes.size())) {
    std::fprintf(stderr, "%s\n", pack.error());
    return 1;
  }
  const size_t heapSize = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 1024 * 1024;
  const unsigned level = argc > 3 ? std::strtoul(argv[3], nullptr, 10) : 0;
  const unsigned steps = argc > 4 ? std::strtoul(argv[4], nullptr, 10) : 24;
  std::vector<uint8_t> heap(heapSize);
  arc::Runtime runtime;
  if (sizeof(void*) == 4) arc::Runtime::setStackLimit(16 * 1024);
  if (!runtime.open(pack, heap.data(), heap.size(), level)) {
    std::fprintf(stderr, "Open: %s\n", runtime.error());
    runtime.close();
    return 1;
  }
  auto emit = [&runtime]() {
    std::printf("{\"level\":%u,\"state\":%u,\"moves\":%u,\"crc\":%u,\"heap\":%zu}\n", runtime.level(), runtime.state(),
                runtime.moves(), arc::Pack::crc32(runtime.frame(), 4096), runtime.heapUsed());
  };
  emit();
  for (unsigned i = 0; i < steps; ++i) {
    unsigned available[7], count = 0;
    for (unsigned action = 1; action <= 7; ++action) {
      if (runtime.actions() & (1u << action)) available[count++] = action;
    }
    if (!count || !runtime.action(i == 12 ? 0 : available[i % count], (i * 17 + 10) % 64, (i * 23 + 10) % 64)) {
      std::fprintf(stderr, "Action %u: %s\n", i, runtime.error());
      runtime.close();
      return 1;
    }
    emit();
  }
  runtime.close();
  // The app restarts and changes games without rebooting the process.
  if (!runtime.open(pack, heap.data(), heap.size(), level)) {
    std::fprintf(stderr, "Reopen: %s\n", runtime.error());
    runtime.close();
    return 1;
  }
  emit();
  runtime.close();
  return 0;
}
