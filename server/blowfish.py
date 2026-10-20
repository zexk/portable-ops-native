"""Blowfish (Schneier, 1993), ECB, with the PSP game's word order: each 8-byte
block is two big-endian 32-bit halves. The initial P-array and S-boxes are the
fractional hexadecimal digits of pi, computed here at import."""


def _pi_words(count):
    """The first `count` 32-bit words of pi's fractional part, by Machin's formula."""
    bits = count * 32 + 64
    one = 1 << bits

    def arctan_inverse(x):
        total, term, n, sign = 0, one // x, 1, 1
        while term:
            total += sign * (term // n)
            term //= x * x
            n += 2
            sign = -sign
        return total

    pi = 16 * arctan_inverse(5) - 4 * arctan_inverse(239)
    fraction = pi - (3 << bits)
    return [(fraction >> (bits - 32 * (i + 1))) & 0xFFFFFFFF for i in range(count)]


_INITIAL = _pi_words(18 + 4 * 256)


class Blowfish:
    def __init__(self, key):
        self.p = _INITIAL[:18]
        self.s = [_INITIAL[18 + 256 * i:18 + 256 * (i + 1)] for i in range(4)]
        for i in range(18):
            word = 0
            for j in range(4):
                word = (word << 8) | key[(i * 4 + j) % len(key)]
            self.p[i] ^= word
        left = right = 0
        for i in range(0, 18, 2):
            left, right = self._encrypt(left, right)
            self.p[i], self.p[i + 1] = left, right
        for box in self.s:
            for i in range(0, 256, 2):
                left, right = self._encrypt(left, right)
                box[i], box[i + 1] = left, right

    def _f(self, x):
        s = self.s
        return ((((s[0][x >> 24] + s[1][(x >> 16) & 0xFF]) & 0xFFFFFFFF) ^ s[2][(x >> 8) & 0xFF]) + s[3][x & 0xFF]) & 0xFFFFFFFF

    def _encrypt(self, left, right):
        for i in range(16):
            left ^= self.p[i]
            right ^= self._f(left)
            left, right = right, left
        left, right = right, left
        return left ^ self.p[17], right ^ self.p[16]

    def _decrypt(self, left, right):
        for i in range(17, 1, -1):
            left ^= self.p[i]
            right ^= self._f(left)
            left, right = right, left
        left, right = right, left
        return left ^ self.p[0], right ^ self.p[1]

    def _blocks(self, data, step):
        out = bytearray()
        for at in range(0, len(data) - len(data) % 8, 8):
            left, right = step(int.from_bytes(data[at:at + 4], 'big'), int.from_bytes(data[at + 4:at + 8], 'big'))
            out += left.to_bytes(4, 'big') + right.to_bytes(4, 'big')
        return bytes(out) + data[len(data) - len(data) % 8:]

    def encrypt(self, data):
        return self._blocks(data, self._encrypt)

    def decrypt(self, data):
        return self._blocks(data, self._decrypt)
