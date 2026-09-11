"""Compact, lossless storage for the pinned games' detached undo snapshots.

Only history entries are encoded. Live game actors stay in the VM, and their
integer identity keys remain unchanged. Entries become ordinary objects again
before game code reads or mutates them.
"""
import _arc_native


class Frozen:
    def __init__(self, value):
        data, refs, memo, pool = bytearray(), [], {}, {}

        def uint(number):
            while number >= 128:
                data.append((number & 127) | 128)
                number >>= 7
            data.append(number)

        def ref(value):
            key = (type(value), value) if isinstance(value, (str, bytes, float)) else id(value)
            if key not in pool:
                pool[key] = len(refs)
                refs.append(value)
            uint(pool[key])

        def write(value):
            if value is None:
                data.append(0)
            elif isinstance(value, bool):
                data.append(1 if value else 2)
            elif isinstance(value, int):
                data.append(3)
                uint(value * 2 if value >= 0 else -value * 2 - 1)
            elif isinstance(value, (str, bytes, float, type)) or callable(value):
                data.append(4)
                ref(value)
            elif id(value) in memo:
                data.append(5)
                uint(memo[id(value)])
            else:
                memo[id(value)] = len(memo)
                if isinstance(value, (list, tuple, set)):
                    data.append(6 if isinstance(value, list) else 7 if isinstance(value, tuple) else 8)
                    uint(len(value))
                    for item in value:
                        write(item)
                elif isinstance(value, dict):
                    data.append(9)
                    uint(len(value))
                    for key, item in value.items():
                        write(key)
                        write(item)
                elif hasattr(value, 'dtype'):
                    # Packed arrays preserve all numeric bytes, shape and dtype.
                    data.append(10)
                    packed = _arc_native.pack_array(value)
                    uint(len(packed))
                    data.extend(packed)
                else:
                    data.append(11)
                    ref(type(value))
                    fields = value.__dict__
                    uint(len(fields))
                    for key, item in fields.items():
                        ref(key)
                        write(item)

        write(value)
        self.data, self.refs = bytes(data), refs

    def thaw(self):
        data, refs, memo = self.data, self.refs, []
        position = 0

        def uint():
            nonlocal position
            value, shift = 0, 0
            while True:
                byte = data[position]
                position += 1
                value |= (byte & 127) << shift
                if byte < 128:
                    return value
                shift += 7

        def read():
            nonlocal position
            tag = data[position]
            position += 1
            if tag == 0:
                return None
            if tag in (1, 2):
                return tag == 1
            if tag == 3:
                value = uint()
                return value // 2 if value % 2 == 0 else -(value // 2) - 1
            if tag == 4:
                return refs[uint()]
            if tag == 5:
                return memo[uint()]
            index = len(memo)
            if tag in (6, 7, 8):
                value = []
                memo.append(value)
                for _ in range(uint()):
                    value.append(read())
                if tag != 6:
                    value = tuple(value) if tag == 7 else set(value)
                    memo[index] = value
            elif tag == 9:
                value = {}
                memo.append(value)
                for _ in range(uint()):
                    key = read()
                    value[key] = read()
            elif tag == 10:
                size = uint()
                value = _arc_native.unpack_array(memoryview(data)[position:position + size])
                position += size
                memo.append(value)
            elif tag == 11:
                cls = refs[uint()]
                value = _arc_native.new_instance(cls)
                memo.append(value)
                for _ in range(uint()):
                    key = refs[uint()]
                    setattr(value, key, read())
            else:
                raise ValueError('Invalid history data')
            return value

        return read()


class History:
    def __init__(self):
        self.items = []

    def __len__(self):
        return len(self.items)

    def __getitem__(self, key):
        value = self.items[key]
        if isinstance(value, Frozen):
            value = self.items[key] = value.thaw()
        return value

    def __iter__(self):
        for i in range(len(self)):
            yield self[i]

    def append(self, value):
        self.items.append(value)

    def pop(self, index=-1):
        value = self[index]
        self.items.pop(index)
        return value

    def clear(self):
        self.items.clear()

    def compact(self):
        for i, value in enumerate(self.items):
            if not isinstance(value, Frozen):
                self.items[i] = Frozen(value)
