"""NumPy operations used by ARC games, backed by native ulab arrays."""
from ulab import numpy as _np
import builtins as _builtins
import _arc_native
globals().update({name: getattr(_np, name) for name in dir(_np) if not name.startswith("_")})

ndarray = _np.ndarray
int32 = int16
int64 = int16
float32 = _np.float
float64 = _np.float


def _dtype(dtype):
    if dtype is _builtins.int:
        return int16
    if dtype is _builtins.float:
        return _np.float
    if dtype is _builtins.bool:
        return _np.bool
    return dtype


def array(value, dtype=None):
    # ulab's integer constructor rejects float inputs; NumPy converts them.
    result = _np.array(value)
    return result if dtype is None else _arc_native.cast(result, _dtype(dtype))


def full(shape, value, dtype=None):
    if dtype is object:
        return _ObjectArray(shape, value)
    return _np.full(shape, value, dtype=_np.float if dtype is None else _dtype(dtype))


def zeros(shape, dtype=None):
    return _np.zeros(shape, dtype=_np.float if dtype is None else _dtype(dtype))


def ones(shape, dtype=None):
    return _np.ones(shape, dtype=_np.float if dtype is None else _dtype(dtype))


class _ObjectArray:
    def __init__(self, shape, value):
        self.shape = tuple(shape)
        self.items = [value] * (shape[0] * shape[1])

    def _offset(self, key):
        row, col = key
        if row < 0:
            row += self.shape[0]
        if col < 0:
            col += self.shape[1]
        if not 0 <= row < self.shape[0] or not 0 <= col < self.shape[1]:
            raise IndexError('Array index out of range')
        return row * self.shape[1] + col

    def __getitem__(self, key):
        return self.items[self._offset(key)]

    def __setitem__(self, key, value):
        self.items[self._offset(key)] = value


def arange(*args, dtype=None):
    if dtype is None:
        dtype = int16 if _builtins.all(isinstance(arg, int) for arg in args) else _np.float
    return _np.arange(*args, dtype=_dtype(dtype))


class _Random:
    def __init__(self):
        self.seed(0)

    def seed(self, seed):
        from arc_random import Random
        self.generator = Random(0)
        s = self.generator.state
        s[0] = seed & 0xffffffff
        for i in range(1, 624):
            s[i] = (1812433253 * (s[i-1] ^ (s[i-1] >> 30)) + i) & 0xffffffff
        self.generator.index = 624

    def shuffle(self, values):
        for i in range(len(values) - 1, 0, -1):
            mask = i
            mask |= mask >> 1
            mask |= mask >> 2
            mask |= mask >> 4
            mask |= mask >> 8
            mask |= mask >> 16
            j = self.generator.getrandbits(32) & mask
            while j > i:
                j = self.generator.getrandbits(32) & mask
            values[i], values[j] = values[j], values[i]


random = _Random()


def scalar_int8(value):
    return (int(value) + 128) % 256 - 128


def scalar_uint8(value):
    return int(value) % 256


def scalar_int16(value):
    return (int(value) + 32768) % 65536 - 32768


def scalar_uint16(value):
    return int(value) % 65536


def astype(value, dtype, copy=True):
    return _arc_native.cast(value, _dtype(dtype))


def rot90(value, k=1):
    k %= 4
    if k == 0:
        return value.copy()
    if k == 1:
        return value.transpose()[::-1, :]
    if k == 2:
        return value[::-1, ::-1]
    return value.transpose()[:, ::-1]


def flipud(value):
    return value[::-1, :]


def fliplr(value):
    return value[:, ::-1]


def ravel(value):
    return value.reshape((value.size,))


def fill(value, item):
    value[:] = item


def tolist(value):
    return [list(row) for row in value] if value.ndim == 2 else list(value)


def array_equal(left, right):
    return left.shape == right.shape and all(left == right)


def zeros_like(value, dtype=None):
    return zeros(value.shape, dtype=value.dtype if dtype is None else dtype)


def ones_like(value, dtype=None):
    return ones(value.shape, dtype=value.dtype if dtype is None else dtype)


def vstack(values):
    return concatenate(values, axis=0)


def prod(value):
    result = 1
    for item in value:
        result *= item
    return result


def sign(value):
    if isinstance(value, ndarray):
        return _np.where(value > 0, 1, _np.where(value < 0, -1, 0))
    return 1 if value > 0 else -1 if value < 0 else 0


def unique(value):
    return array(_builtins.sorted(set(value.flatten())), dtype=value.dtype)


def argwhere(value):
    rows = []
    if value.ndim == 1:
        rows = [[i] for i in range(value.shape[0]) if value[i]]
    else:
        rows = [[r, c] for r in range(value.shape[0]) for c in range(value.shape[1]) if value[r, c]]
    return array(rows, dtype=int16) if rows else _np.zeros((0, value.ndim), dtype=int16)


def round(value):
    return _arc_native.round(value) if isinstance(value, ndarray) else _builtins.round(value)
