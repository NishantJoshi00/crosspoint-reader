from collections import namedtuple, OrderedDict


class deque:
    """Unbounded queue with amortized constant-time removal from the left."""
    def __init__(self, iterable=(), maxlen=None):
        self.items = list(iterable)
        self.start = 0
        self.maxlen = maxlen

    def __len__(self):
        return len(self.items) - self.start

    def __bool__(self):
        return len(self) != 0

    def __iter__(self):
        return iter(self.items[self.start:])

    def append(self, value):
        self.items.append(value)
        if self.maxlen is not None and len(self) > self.maxlen:
            self.popleft()

    def popleft(self):
        if not self:
            raise IndexError('pop from an empty deque')
        value = self.items[self.start]
        self.items[self.start] = None
        self.start += 1
        if self.start >= 32 and self.start * 2 >= len(self.items):
            self.items = self.items[self.start:]
            self.start = 0
        return value

    def pop(self):
        if not self:
            raise IndexError('pop from an empty deque')
        return self.items.pop()

    def clear(self):
        self.items = []
        self.start = 0
