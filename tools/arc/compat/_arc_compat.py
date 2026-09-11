import builtins
import math


def dist(left, right):
    if len(left) != len(right):
        raise ValueError('Both points must have the same number of dimensions')
    return math.sqrt(sum((a - b) ** 2 for a, b in zip(left, right)))


def last_frame(frames, frame):
    frames[:] = [frame]


def sorted(iterable, key=None, reverse=False):
    # MicroPython's quicksort is not stable. ARC relies on Python's stable ties
    # for sprite layers, click hit-testing, and rule evaluation order.
    decorated = [(value if key is None else key(value), -i if reverse else i, value)
                 for i, value in enumerate(iterable)]
    decorated.sort(key=lambda entry: (entry[0], entry[1]), reverse=reverse)
    return [entry[2] for entry in decorated]


def sort(values, key=None, reverse=False):
    values[:] = sorted(values, key=key, reverse=reverse)


def mro(cls):
    bases = cls.__bases__
    sequences = [list(mro(base)) for base in bases] + [list(bases)]
    result = [cls]
    while True:
        sequences = [seq for seq in sequences if seq]
        if not sequences:
            return tuple(result)
        for seq in sequences:
            candidate = seq[0]
            if not any(candidate in other[1:] for other in sequences):
                break
        else:
            raise TypeError('Inconsistent method resolution order')
        result.append(candidate)
        for seq in sequences:
            if seq[0] is candidate:
                seq.pop(0)


class SpriteFactories:
    def __init__(self, factories):
        self.factories = factories
        self.cache = {}

    def __getitem__(self, key):
        if key not in self.cache:
            self.cache[key] = self.factories[key]()
        return self.cache[key]

    def __iter__(self):
        return iter(self.factories)

    def __len__(self):
        return len(self.factories)

    def __contains__(self, key):
        return key in self.factories

    def keys(self):
        return self.factories.keys()

    def values(self):
        return [self[key] for key in self]

    def items(self):
        return [(key, self[key]) for key in self]


class LevelFactories:
    def __init__(self, factories):
        self.factories = factories

    def __len__(self):
        return len(self.factories)

    def __getitem__(self, key):
        return self.factories[key]()


class Levels:
    def __init__(self, source):
        self.source = source.source if isinstance(source, Levels) else source
        self.active = None
        self.index = None

    def __len__(self):
        return len(self.source)

    def __getitem__(self, key):
        if not 0 <= key < len(self):
            raise IndexError('Level index out of range')
        if self.index != key:
            self.active = self.source[key].clone()
            self.index = key
        return self.active

    def __setitem__(self, key, value):
        if not 0 <= key < len(self):
            raise IndexError('Level index out of range')
        self.index, self.active = key, value
