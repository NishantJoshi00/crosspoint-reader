#include "ArcRuntime.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// clang-format off
extern "C" {
#include "py/runtime.h"
#include "ndarray.h"
#include "py/builtin.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "py/reader.h"
#include "py/stackctrl.h"
#include "shared/runtime/gchelper.h"
}
// clang-format on

namespace {
const arc::Pack* activePack = nullptr;
bool (*pollHook)() = nullptr;
size_t stackLimit = 48 * 1024;

struct ErrorWriter {
  char* data;
  size_t used;
  size_t capacity;
};

void writeError(void* context, const char* data, size_t length) {
  auto* writer = static_cast<ErrorWriter*>(context);
  if (length > writer->capacity - writer->used - 1) length = writer->capacity - writer->used - 1;
  std::memcpy(writer->data + writer->used, data, length);
  writer->used += length;
  writer->data[writer->used] = 0;
}

mp_obj_t call(mp_obj_t module, const char* method, size_t count, const mp_obj_t* args) {
  return mp_call_function_n_kw(mp_load_attr(module, qstr_from_str(method)), count, 0, args);
}
}  // namespace

extern "C" {
void arc_vm_poll() {
  if (pollHook && !pollHook()) mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Game interrupted"));
}

void gc_collect(void) {
  gc_collect_start();
  gc_helper_collect_regs_and_stack();
  gc_collect_end();
}

void nlr_jump_fail(void*) { std::abort(); }

void mp_hal_stdout_tx_strn_cooked(const char* data, size_t size) { std::printf("%.*s", int(size), data); }

mp_import_stat_t mp_import_stat(const char* path) {
  if (!activePack) return MP_IMPORT_STAT_NO_EXIST;
  size_t length;
  if (activePack->find(path, length)) return MP_IMPORT_STAT_FILE;
  return activePack->directory(path) ? MP_IMPORT_STAT_DIR : MP_IMPORT_STAT_NO_EXIST;
}

void mp_reader_new_file(mp_reader_t* reader, qstr path) {
  size_t length;
  const auto* data = activePack ? activePack->find(qstr_str(path), length) : nullptr;
  if (!data) mp_raise_OSError(MP_ENOENT);
  mp_reader_new_mem(reader, data, length, MP_READER_IS_ROM);
}
}  // extern "C"

namespace arc {

void Runtime::setPollHook(bool (*hook)()) { pollHook = hook; }
void Runtime::setStackLimit(size_t bytes) { stackLimit = bytes; }

bool Runtime::open(const Pack& pack, void* heap, const size_t heapSize, unsigned level) {
  if (active_ || activePack || !heap || heapSize < 32768 || level >= pack.levels()) {
    std::snprintf(error_, sizeof(error_), "Player cannot start with the available memory");
    return false;
  }
  activePack = &pack;
  gc_init(heap, static_cast<uint8_t*>(heap) + heapSize);
  active_ = true;
  char game[17];
  std::snprintf(game, sizeof(game), "%s", pack.gameId());
  if (char* separator = std::strchr(game, '-')) *separator = 0;
  return invoke(true, game, level, 0, 0);
}

bool Runtime::action(unsigned action, unsigned x, unsigned y) {
  if (!active_) return false;
  if (action > 7 || x > 63 || y > 63 || (action != 0 && !(actions_ & (1u << action)))) {
    std::snprintf(error_, sizeof(error_), "This action is unavailable");
    return false;
  }
  return invoke(false, nullptr, action, x, y);
}

bool Runtime::invoke(bool opening, const char* game, unsigned first, unsigned second, unsigned third) {
  int stackTop;
  mp_stack_set_top(&stackTop);
  mp_stack_set_limit(stackLimit);
  nlr_buf_t nlr;
  if (nlr_push(&nlr) == 0) {
    if (opening) mp_init();
    mp_obj_t module = mp_import_name(qstr_from_str("_arc_boot"), mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
    mp_obj_t args[3] = {mp_obj_new_int(first), mp_obj_new_int(second), mp_obj_new_int(third)};
    mp_obj_t result;
    if (opening) {
      args[0] = mp_obj_new_str(game, std::strlen(game));
      args[1] = mp_obj_new_int(first);
      result = call(module, "open_game", 2, args);
    } else {
      result = call(module, "act", 3, args);
    }
    if (!mp_obj_is_type(result, &ulab_ndarray_type)) mp_raise_ValueError(MP_ERROR_TEXT("Invalid game frame"));
    auto* pixels = static_cast<ndarray_obj_t*>(MP_OBJ_TO_PTR(result));
    if (pixels->ndim != 2 || pixels->shape[ULAB_MAX_DIMS - 2] != 64 || pixels->shape[ULAB_MAX_DIMS - 1] != 64)
      mp_raise_ValueError(MP_ERROR_TEXT("Game frame must be 64 by 64"));
    for (int row = 0; row < 64; ++row) {
      for (int col = 0; col < 64; ++col) {
        const auto* ptr = static_cast<const uint8_t*>(pixels->array) + row * pixels->strides[ULAB_MAX_DIMS - 2] +
                          col * pixels->strides[ULAB_MAX_DIMS - 1];
        int value = mp_obj_get_int(ndarray_get_item(pixels, const_cast<uint8_t*>(ptr)));
        if (value < 0 || value > 15) mp_raise_ValueError(MP_ERROR_TEXT("Game frame contains an invalid color"));
        frame_[row * 64 + col] = value;
      }
    }
    mp_obj_t information = call(module, "info", 0, nullptr);
    size_t length;
    mp_obj_t* items;
    mp_obj_get_array(information, &length, &items);
    if (length != 5) mp_raise_ValueError(MP_ERROR_TEXT("Invalid game state"));
    state_ = mp_obj_get_int(items[0]);
    level_ = mp_obj_get_int(items[1]);
    levels_ = mp_obj_get_int(items[2]);
    actions_ = mp_obj_get_int(items[3]);
    moves_ = mp_obj_get_int(items[4]);
    // Python action frames have unwound. Collect here as well as in the boot
    // module so dead temporary roots in those frames do not survive a move.
    gc_collect();
    error_[0] = 0;
    nlr_pop();
    return true;
  }
  ErrorWriter writer{error_, 0, sizeof(error_)};
#ifdef ARC_HOST_TRACE
  mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
  gc_info_t stats;
  gc_info(&stats);
  std::printf("VM heap total=%zu used=%zu free=%zu max_free=%zu\n", stats.total, stats.used, stats.free,
              stats.max_free);
#endif
  mp_print_t printer{&writer, writeError};
  mp_obj_print_helper(&printer, MP_OBJ_FROM_PTR(nlr.ret_val), PRINT_EXC);
  return false;
}

void Runtime::close() {
  if (!active_) return;
  int stackTop;
  mp_stack_set_top(&stackTop);
  mp_deinit();
  activePack = nullptr;
  active_ = false;
}

size_t Runtime::heapUsed() const {
  if (!active_) return 0;
  gc_info_t info;
  gc_info(&info);
  return info.used;
}
}  // namespace arc
