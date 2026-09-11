"""MT19937 integer-seed/choice/randint semantics used by CPython random.Random.

The initialization and twist follow Matsumoto and Nishimura's published MT19937
algorithm. Integer seeds use CPython's little-endian 32-bit seed words.
"""
from array import array


class Random:
    def __init__(self, seed):
        self.state = array('I', [0] * 624)
        self.seed(seed)

    def seed(self, seed):
        seed = abs(int(seed))
        words = []
        while seed:
            words.append(seed & 0xffffffff)
            seed >>= 32
        if not words:
            words = [0]
        s = self.state
        s[0] = 19650218
        for i in range(1, 624):
            s[i] = (1812433253 * (s[i-1] ^ (s[i-1] >> 30)) + i) & 0xffffffff
        i, j = 1, 0
        for _ in range(max(624, len(words))):
            s[i] = ((s[i] ^ ((s[i-1] ^ (s[i-1] >> 30)) * 1664525)) + words[j] + j) & 0xffffffff
            i += 1
            j = (j + 1) % len(words)
            if i == 624:
                s[0], i = s[623], 1
        for _ in range(623):
            s[i] = ((s[i] ^ ((s[i-1] ^ (s[i-1] >> 30)) * 1566083941)) - i) & 0xffffffff
            i += 1
            if i == 624:
                s[0], i = s[623], 1
        s[0] = 0x80000000
        self.index = 624

    def getrandbits(self, bits):
        if not 0 <= bits <= 32:
            raise ValueError('getrandbits supports 0..32 bits')
        if bits == 0:
            return 0
        s = self.state
        if self.index == 624:
            for i in range(624):
                value = (s[i] & 0x80000000) | (s[(i+1) % 624] & 0x7fffffff)
                s[i] = s[(i+397) % 624] ^ (value >> 1) ^ (0x9908b0df if value & 1 else 0)
            self.index = 0
        value = s[self.index]
        self.index += 1
        value ^= value >> 11
        value ^= (value << 7) & 0x9d2c5680
        value ^= (value << 15) & 0xefc60000
        value ^= value >> 18
        return value >> (32 - bits)

    def _randbelow(self, n):
        bits = 0
        value = n
        while value:
            bits += 1
            value >>= 1
        value = self.getrandbits(bits)
        while value >= n:
            value = self.getrandbits(bits)
        return value

    def choice(self, values):
        if not values:
            raise IndexError('Cannot choose from an empty sequence')
        return values[self._randbelow(len(values))]

    def randint(self, a, b):
        if b < a:
            raise ValueError('Empty range')
        return a + self._randbelow(b - a + 1)
