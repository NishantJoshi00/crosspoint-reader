// Native hot paths for the ARC SDK compatibility module.
// ulab's headers require MicroPython's runtime declarations first.
// clang-format off
extern "C" {
#include "py/runtime.h"
#include "ndarray.h"
#include "py/objlist.h"
#include "py/objstr.h"
#include "py/objtype.h"
}
// clang-format on

#include <cmath>
#include <cstring>

#include "ArcGraphics.h"

namespace {

int integer(mp_obj_t obj, qstr key) { return mp_obj_get_int(mp_load_attr(obj, key)); }
bool boolean(mp_obj_t obj, qstr key) { return mp_obj_is_true(mp_load_attr(obj, key)); }

ndarray_obj_t* array(mp_obj_t obj) {
  if (!mp_obj_is_type(obj, &ulab_ndarray_type)) mp_raise_TypeError(MP_ERROR_TEXT("Expected ndarray"));
  return static_cast<ndarray_obj_t*>(MP_OBJ_TO_PTR(obj));
}

ndarray_obj_t* matrix(int height, int width) {
  if (height < 1 || width < 1 || height > 256 || width > 256)
    mp_raise_ValueError(MP_ERROR_TEXT("ARC array dimensions out of range"));
  size_t shape[ULAB_MAX_DIMS] = {};
  shape[ULAB_MAX_DIMS - 2] = height;
  shape[ULAB_MAX_DIMS - 1] = width;
  return ndarray_new_dense_ndarray(2, shape, NDARRAY_INT8);
}

arc::SpriteView view(mp_obj_t obj) {
  ndarray_obj_t* a = array(mp_load_attr(obj, MP_QSTR_pixels));
  if (a->ndim != 2) mp_raise_ValueError(MP_ERROR_TEXT("Sprite pixels must be a 2D array"));
  arc::SpriteView s;
  s.pixels = static_cast<const int8_t*>(a->array);
  s.rows = a->shape[ULAB_MAX_DIMS - 2];
  s.cols = a->shape[ULAB_MAX_DIMS - 1];
  s.rowStride = a->strides[ULAB_MAX_DIMS - 2];
  s.colStride = a->strides[ULAB_MAX_DIMS - 1];
  if (s.rows < 1 || s.cols < 1 || s.rows > 256 || s.cols > 256)
    mp_raise_ValueError(MP_ERROR_TEXT("Sprite dimensions out of range"));
  if (a->dtype != NDARRAY_INT8) {
    auto* converted = static_cast<int8_t*>(m_malloc(s.rows * s.cols));
    for (int row = 0; row < s.rows; ++row) {
      for (int col = 0; col < s.cols; ++col) {
        const auto* ptr = static_cast<const uint8_t*>(a->array) + row * s.rowStride + col * s.colStride;
        double value = 0;
        switch (a->dtype) {
          case NDARRAY_UINT8:
            value = *ptr;
            break;
          case NDARRAY_INT16: {
            int16_t v;
            std::memcpy(&v, ptr, sizeof(v));
            value = v;
            break;
          }
          case NDARRAY_UINT16: {
            uint16_t v;
            std::memcpy(&v, ptr, sizeof(v));
            value = v;
            break;
          }
          case NDARRAY_FLOAT: {
            mp_float_t v;
            std::memcpy(&v, ptr, sizeof(v));
            value = v;
            break;
          }
          default:
            mp_raise_ValueError(MP_ERROR_TEXT("Unsupported sprite pixel type"));
        }
        if (!std::isfinite(value) || value < -2 || value > 15 || value != static_cast<int>(value))
          mp_raise_ValueError(MP_ERROR_TEXT("Invalid ARC palette index"));
        converted[row * s.cols + col] = static_cast<int8_t>(value);
      }
    }
    s.pixels = converted;
    s.rowStride = s.cols;
    s.colStride = 1;
  }
  s.rotation = integer(obj, MP_QSTR_rotation);
  s.scale = integer(obj, MP_QSTR_scale);
  s.mirrorX = boolean(obj, MP_QSTR_mirror_lr);
  s.mirrorY = boolean(obj, MP_QSTR_mirror_ud);
  s.x = integer(obj, MP_QSTR_x);
  s.y = integer(obj, MP_QSTR_y);
  s.blocking = mp_obj_get_int(mp_load_attr(mp_load_attr(obj, MP_QSTR_blocking), MP_QSTR_value));
  s.collidable = boolean(obj, MP_QSTR_is_collidable);
  if (!s.valid()) mp_raise_ValueError(MP_ERROR_TEXT("Invalid ARC sprite transform"));
  return s;
}

mp_obj_t spriteRender(mp_obj_t obj) {
  const auto s = view(obj);
  auto* result = matrix(s.height(), s.width());
  auto* pixels = static_cast<int8_t*>(result->array);
  for (int y = 0; y < s.height(); ++y) {
    for (int x = 0; x < s.width(); ++x) pixels[y * s.width() + x] = s.sample(y, x);
  }
  return MP_OBJ_FROM_PTR(result);
}
MP_DEFINE_CONST_FUN_OBJ_1(spriteRenderObj, spriteRender);

mp_obj_t spriteCollision(mp_obj_t left, mp_obj_t right, mp_obj_t ignore) {
  if (left == right) return mp_const_false;
  return mp_obj_new_bool(arc::collide(view(left), view(right), mp_obj_is_true(ignore)));
}
MP_DEFINE_CONST_FUN_OBJ_3(spriteCollisionObj, spriteCollision);

mp_obj_t rawCamera(mp_obj_t camera, mp_obj_t sprites) {
  const int width = integer(camera, MP_QSTR_width);
  const int height = integer(camera, MP_QSTR_height);
  auto* result = matrix(height, width);
  auto* pixels = static_cast<int8_t*>(result->array);
  std::memset(pixels, integer(camera, MP_QSTR_background), width * height);
  const int x = integer(camera, MP_QSTR_x);
  const int y = integer(camera, MP_QSTR_y);
  size_t count;
  mp_obj_t* items;
  mp_obj_get_array(sprites, &count, &items);
  for (size_t i = 0; i < count; ++i) {
    if (boolean(items[i], MP_QSTR_is_visible)) arc::paint(pixels, width, height, view(items[i]), x, y);
  }
  return MP_OBJ_FROM_PTR(result);
}
MP_DEFINE_CONST_FUN_OBJ_2(rawCameraObj, rawCamera);

mp_obj_t scaleCamera(mp_obj_t camera, mp_obj_t raw) {
  const auto* source = array(raw);
  auto* result = matrix(64, 64);
  arc::scaleCamera(static_cast<const int8_t*>(source->array), integer(camera, MP_QSTR_width),
                   integer(camera, MP_QSTR_height), static_cast<int8_t*>(result->array),
                   integer(camera, MP_QSTR_letter_box));
  return MP_OBJ_FROM_PTR(result);
}
MP_DEFINE_CONST_FUN_OBJ_2(scaleCameraObj, scaleCamera);

mp_obj_t fromBytes(mp_obj_t buffer, mp_obj_t heightObj, mp_obj_t widthObj) {
  mp_buffer_info_t info;
  mp_get_buffer_raise(buffer, &info, MP_BUFFER_READ);
  const int height = mp_obj_get_int(heightObj);
  const int width = mp_obj_get_int(widthObj);
  auto* result = matrix(height, width);
  if (info.len != result->len) mp_raise_ValueError(MP_ERROR_TEXT("ARC pixel data length mismatch"));
  std::memcpy(result->array, info.buf, info.len);
  return MP_OBJ_FROM_PTR(result);
}
MP_DEFINE_CONST_FUN_OBJ_3(fromBytesObj, fromBytes);

mp_obj_t castArray(mp_obj_t value, mp_obj_t dtype) {
  return MP_OBJ_FROM_PTR(ndarray_copy_view_convert_type(array(value), mp_obj_get_int(dtype)));
}
MP_DEFINE_CONST_FUN_OBJ_2(castArrayObj, castArray);

mp_obj_t superValue(mp_obj_t value, mp_obj_t self) {
  if (mp_obj_is_type(value, &mp_type_property)) return mp_call_function_1(mp_obj_property_get(value)[0], self);
  return value;
}
MP_DEFINE_CONST_FUN_OBJ_2(superValueObj, superValue);

mp_obj_t roundArray(mp_obj_t value) {
  auto* source = array(value);
  auto* result = ndarray_copy_view_convert_type(source, source->dtype);
  if (result->dtype == NDARRAY_FLOAT) {
    auto* data = static_cast<mp_float_t*>(result->array);
    for (size_t i = 0; i < result->len; ++i) {
      if (!std::isfinite(data[i])) continue;
      const mp_float_t low = std::floor(data[i]);
      const mp_float_t fraction = data[i] - low;
      data[i] = fraction < 0.5 ? low : fraction > 0.5 ? low + 1 : (std::fmod(low, 2.0) == 0 ? low : low + 1);
    }
  }
  return MP_OBJ_FROM_PTR(result);
}
MP_DEFINE_CONST_FUN_OBJ_1(roundArrayObj, roundArray);

mp_obj_t affine(size_t, const mp_obj_t* args) {
  auto* source = array(args[0]);
  if (source->ndim != 2 || source->dtype != NDARRAY_INT8)
    mp_raise_ValueError(MP_ERROR_TEXT("Affine input must be a palette matrix"));
  auto* output = matrix(mp_obj_get_int(args[2]), mp_obj_get_int(args[1]));
  const double cx = mp_obj_get_float(args[3]), cy = mp_obj_get_float(args[4]);
  const double sine = mp_obj_get_float(args[5]), cosine = mp_obj_get_float(args[6]);
  const double ax = mp_obj_get_float(args[7]), ay = mp_obj_get_float(args[8]);
  const int width = output->shape[ULAB_MAX_DIMS - 1], height = output->shape[ULAB_MAX_DIMS - 2];
  const int sw = source->shape[ULAB_MAX_DIMS - 1], sh = source->shape[ULAB_MAX_DIMS - 2];
  const auto* pixels = static_cast<const int8_t*>(source->array);
  auto* target = static_cast<int8_t*>(output->array);
  auto nearest = [](double value) {
    const double low = std::floor(value), fraction = value - low;
    return static_cast<int>(fraction < 0.5 ? low : fraction > 0.5 ? low + 1 : std::fmod(low, 2.0) == 0 ? low : low + 1);
  };
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const double dx = x - cx, dy = y - cy;
      const int sx = nearest(dx * cosine + dy * sine + ax);
      const int sy = nearest(-dx * sine + dy * cosine + ay);
      target[y * width + x] =
          sx >= 0 && sx < sw && sy >= 0 && sy < sh
              ? pixels[sy * source->strides[ULAB_MAX_DIMS - 2] + sx * source->strides[ULAB_MAX_DIMS - 1]]
              : -1;
    }
  }
  return MP_OBJ_FROM_PTR(output);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(affineObj, 9, 9, affine);

mp_obj_t packArray(mp_obj_t value) {
  const auto* source = array(value);
  if (source->ndim < 1 || source->ndim > 2) mp_raise_ValueError(MP_ERROR_TEXT("Invalid history array"));
  const size_t rows = source->ndim == 2 ? source->shape[ULAB_MAX_DIMS - 2] : 1;
  const size_t cols = source->shape[ULAB_MAX_DIMS - 1];
  if (rows > 65535 || cols > 65535) mp_raise_ValueError(MP_ERROR_TEXT("History array is too large"));
  const size_t length = source->len * source->itemsize;
  auto byteAt = [source, cols](size_t index) {
    const size_t cell = index / source->itemsize;
    return *(static_cast<const uint8_t*>(source->array) + (cell / cols) * source->strides[ULAB_MAX_DIMS - 2] +
             (cell % cols) * source->strides[ULAB_MAX_DIMS - 1] + index % source->itemsize);
  };
  size_t encoded = 0;
  for (size_t i = 0; i < length;) {
    const uint8_t byte = byteAt(i++);
    size_t count = 1;
    while (i < length && count < 255 && byteAt(i) == byte) {
      ++i;
      ++count;
    }
    encoded += 2;
  }
  const bool compressed = encoded < length;
  vstr_t output;
  vstr_init_len(&output, 8 + (compressed ? encoded : length));
  auto* bytes = reinterpret_cast<uint8_t*>(output.buf);
  bytes[0] = source->ndim;
  bytes[1] = source->boolean == NDARRAY_BOOLEAN ? NDARRAY_BOOL : source->dtype;
  bytes[2] = source->itemsize;
  bytes[3] = compressed;
  bytes[4] = rows;
  bytes[5] = rows >> 8;
  bytes[6] = cols;
  bytes[7] = cols >> 8;
  size_t offset = 8;
  for (size_t i = 0; i < length;) {
    const uint8_t byte = byteAt(i++);
    if (compressed) {
      size_t count = 1;
      while (i < length && count < 255 && byteAt(i) == byte) {
        ++i;
        ++count;
      }
      bytes[offset++] = count;
    }
    bytes[offset++] = byte;
  }
  return mp_obj_new_bytes_from_vstr(&output);
}
MP_DEFINE_CONST_FUN_OBJ_1(packArrayObj, packArray);

mp_obj_t unpackArray(mp_obj_t value) {
  mp_buffer_info_t buffer;
  mp_get_buffer_raise(value, &buffer, MP_BUFFER_READ);
  const auto* bytes = static_cast<const uint8_t*>(buffer.buf);
  if (buffer.len < 8 || bytes[0] < 1 || bytes[0] > 2) mp_raise_ValueError(MP_ERROR_TEXT("Invalid history array"));
  const uint8_t dtype = bytes[1];
  if (dtype != NDARRAY_BOOL && dtype != NDARRAY_INT8 && dtype != NDARRAY_UINT8 && dtype != NDARRAY_INT16 &&
      dtype != NDARRAY_UINT16 && dtype != NDARRAY_FLOAT)
    mp_raise_ValueError(MP_ERROR_TEXT("Invalid history dtype"));
  size_t shape[ULAB_MAX_DIMS] = {};
  shape[ULAB_MAX_DIMS - 1] = bytes[6] | (size_t(bytes[7]) << 8);
  if (bytes[0] == 2) shape[ULAB_MAX_DIMS - 2] = bytes[4] | (size_t(bytes[5]) << 8);
  auto* output = ndarray_new_dense_ndarray(bytes[0], shape, dtype);
  const size_t length = output->len * output->itemsize;
  if (output->itemsize != bytes[2]) mp_raise_ValueError(MP_ERROR_TEXT("Invalid history item size"));
  auto* target = static_cast<uint8_t*>(output->array);
  size_t position = 0;
  for (size_t i = 8; i < buffer.len;) {
    const size_t count = bytes[3] ? bytes[i++] : 1;
    if (count == 0 || count > length - position || i == buffer.len)
      mp_raise_ValueError(MP_ERROR_TEXT("Invalid history data"));
    std::memset(target + position, bytes[i++], count);
    position += count;
  }
  if (position != length) mp_raise_ValueError(MP_ERROR_TEXT("Truncated history array"));
  return MP_OBJ_FROM_PTR(output);
}
MP_DEFINE_CONST_FUN_OBJ_1(unpackArrayObj, unpackArray);

mp_obj_t newInstance(mp_obj_t cls) {
  if (!mp_obj_is_type(cls, &mp_type_type) ||
      !mp_obj_is_instance_type(static_cast<const mp_obj_type_t*>(MP_OBJ_TO_PTR(cls))))
    mp_raise_TypeError(MP_ERROR_TEXT("Expected a history class"));
  const mp_obj_type_t* nativeBase = nullptr;
  auto* instance = mp_obj_new_instance(static_cast<const mp_obj_type_t*>(MP_OBJ_TO_PTR(cls)), &nativeBase);
  if (nativeBase) mp_raise_TypeError(MP_ERROR_TEXT("Native history classes are unsupported"));
  return MP_OBJ_FROM_PTR(instance);
}
MP_DEFINE_CONST_FUN_OBJ_1(newInstanceObj, newInstance);

const mp_rom_map_elem_t globals[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__arc_native)},
    {MP_ROM_QSTR(MP_QSTR_sprite_render), MP_ROM_PTR(&spriteRenderObj)},
    {MP_ROM_QSTR(MP_QSTR_sprite_collision), MP_ROM_PTR(&spriteCollisionObj)},
    {MP_ROM_QSTR(MP_QSTR_camera_raw), MP_ROM_PTR(&rawCameraObj)},
    {MP_ROM_QSTR(MP_QSTR_camera_scale), MP_ROM_PTR(&scaleCameraObj)},
    {MP_ROM_QSTR(MP_QSTR_matrix), MP_ROM_PTR(&fromBytesObj)},
    {MP_ROM_QSTR(MP_QSTR_cast), MP_ROM_PTR(&castArrayObj)},
    {MP_ROM_QSTR(MP_QSTR_super_value), MP_ROM_PTR(&superValueObj)},
    {MP_ROM_QSTR(MP_QSTR_round), MP_ROM_PTR(&roundArrayObj)},
    {MP_ROM_QSTR(MP_QSTR_affine), MP_ROM_PTR(&affineObj)},
    {MP_ROM_QSTR(MP_QSTR_pack_array), MP_ROM_PTR(&packArrayObj)},
    {MP_ROM_QSTR(MP_QSTR_unpack_array), MP_ROM_PTR(&unpackArrayObj)},
    {MP_ROM_QSTR(MP_QSTR_new_instance), MP_ROM_PTR(&newInstanceObj)},
};
MP_DEFINE_CONST_DICT(globalsDict, globals);
}  // namespace

extern "C" {
extern const mp_obj_module_t arcNativeModule = {{&mp_type_module}, const_cast<mp_obj_dict_t*>(&globalsDict)};
MP_REGISTER_MODULE(MP_QSTR__arc_native, arcNativeModule);
}
