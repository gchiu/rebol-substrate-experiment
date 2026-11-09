"""experiments/glon-deflate/tests/fixtures.py -- the DEFLATE conformance corpus.

Every compressed fixture is produced INDEPENDENTLY of Glon: by Python's zlib
(the C zlib library) or by the small RFC 1951 bit writer below (for streams zlib
never emits: hand-made stored blocks, single-code trees and malformed input).
The expected result of every fixture comes from zlib's decompressor, never from
the Glon decoder.

fixtures() returns a list of (name, compressed bytes, tags). The data is
deterministic (seeded).
"""
import random
import zlib


# ---------------------------------------------------------------- inputs ---
def text(n, seed):
    r = random.Random(seed)
    words = [b"alpha", b"beta", b"gamma", b"delta", b"deflate", b"glon", b"huffman",
             b"window", b"stream", b"iterator", b"\n", b", ", b"Rebol", b"substrate"]
    out = bytearray()
    while len(out) < n:
        out += r.choice(words) + b" "
    return bytes(out[:n])


def noise(n, seed):
    r = random.Random(seed)
    return bytes(r.getrandbits(8) for _ in range(n))


def deflate(data, level=6, wbits=-10, mem=8, strategy=zlib.Z_DEFAULT_STRATEGY, flushes=()):
    """raw DEFLATE of data; flushes is a list of (offset, mode) cutting the
    input into separately flushed parts (each flush ends a block)"""
    c = zlib.compressobj(level, zlib.DEFLATED, wbits, mem, strategy)
    out, pos = b"", 0
    for off, mode in flushes:
        out += c.compress(data[pos:off]) + c.flush(mode)
        pos = off
    return out + c.compress(data[pos:]) + c.flush()


# ------------------------------------------------------- RFC 1951 writer ---
class Bits:
    """an LSB-first bit writer, as RFC 1951 3.1.1 packs data"""
    def __init__(self):
        self.out, self.acc, self.n = bytearray(), 0, 0

    def put(self, v, k):              # k bits of v, least significant first
        self.acc |= (v & ((1 << k) - 1)) << self.n
        self.n += k
        while self.n >= 8:
            self.out.append(self.acc & 255)
            self.acc >>= 8
            self.n -= 8

    def code(self, c, k):             # a Huffman code: most significant bit first
        for i in range(k - 1, -1, -1):
            self.put((c >> i) & 1, 1)

    def align(self):
        if self.n:
            self.put(0, 8 - self.n)

    def bytes(self):
        self.align()
        return bytes(self.out)


def canonical(lengths):
    """RFC 1951 3.2.2: code for each symbol from its length"""
    bl = [0] * 16
    for l in lengths:
        if l:
            bl[l] += 1
    code, nxt = 0, [0] * 16
    for b in range(1, 16):
        code = (code + bl[b - 1]) << 1
        nxt[b] = code
    codes = [0] * len(lengths)
    for s, l in enumerate(lengths):
        if l:
            codes[s] = nxt[l]
            nxt[l] += 1
    return codes


FIXED_LIT = [8] * 144 + [9] * 112 + [7] * 24 + [8] * 8
FIXED_LIT_CODES = canonical(FIXED_LIT)


def fixed_sym(w, s):
    w.code(FIXED_LIT_CODES[s], FIXED_LIT[s])


LBASE = [3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258]
LEXT = [0] * 8 + [1] * 4 + [2] * 4 + [3] * 4 + [4] * 4 + [5] * 4 + [0]
DBASE = [1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
         2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577]
DEXT = [0, 0, 0, 0] + [k for k in range(1, 14) for _ in (0, 1)]


def length_code(L):
    i = 28 if L == 258 else max(k for k in range(28) if LBASE[k] <= L)
    return i, L - LBASE[i], LEXT[i]


def dist_code(d):
    i = max(k for k in range(30) if DBASE[k] <= d)
    return i, d - DBASE[i], DEXT[i]


def fixed_match(w, L, d):
    i, v, b = length_code(L)
    fixed_sym(w, 257 + i); w.put(v, b)
    i, v, b = dist_code(d)
    w.code(i, 5); w.put(v, b)


def rle_ops(lens):
    """code-length symbols for lens, using 16 (repeat previous), 17 and 18 (zeros)"""
    ops, i = [], 0
    while i < len(lens):
        v, j = lens[i], i
        while j < len(lens) and lens[j] == v:
            j += 1
        run = j - i
        if v == 0 and run >= 3:
            k = min(run, 138)
            ops.append((18, 7, k - 11) if k >= 11 else (17, 3, k - 3)); i += k
        elif v != 0 and run >= 4:
            ops.append((v, 0, 0)); k = min(run - 1, 6); ops.append((16, 2, k - 3)); i += 1 + k
        else:
            ops.append((v, 0, 0)); i += 1
    return ops


def stored(w, data, last):
    w.put(last, 1); w.put(0, 2); w.align()
    w.put(len(data), 16); w.put(len(data) ^ 0xFFFF, 16)
    for b in data:
        w.put(b, 8)


CL_ORDER = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15]


def dynamic_header(w, last, lit_lens, dist_lens, cl_lens=None, ops=None):
    """a dynamic block header. ops, if given, is the raw code-length symbol
    sequence [(sym, extra_bits, extra_value)]; otherwise lengths are sent plainly"""
    w.put(last, 1); w.put(2, 2)
    w.put(len(lit_lens) - 257, 5); w.put(len(dist_lens) - 1, 5)
    if ops is None:
        ops = [(l, 0, 0) for l in lit_lens + dist_lens]
    if cl_lens is None:                       # a complete code over all 19 symbols
        cl_lens = [4] * 13 + [5] * 6          # 13/16 + 6/32 = 1
    nclen = 19
    w.put(nclen - 4, 4)
    for s in CL_ORDER[:nclen]:
        w.put(cl_lens[s], 3)
    cl_codes = canonical(cl_lens)
    for sym, eb, ev in ops:
        w.code(cl_codes[sym], cl_lens[sym])
        if eb:
            w.put(ev, eb)


def dynamic_block(w, last, lit_lens, dist_lens, symbols, rle=False):
    """symbols: [('lit', b) | ('eob',) | ('match', lcode, lextra_bits, lextra, dcode, dextra_bits, dextra)]"""
    dynamic_header(w, last, lit_lens, dist_lens, ops=rle_ops(lit_lens + dist_lens) if rle else None)
    lc, dc = canonical(lit_lens), canonical(dist_lens)
    for s in symbols:
        if s[0] == "lit":
            w.code(lc[s[1]], lit_lens[s[1]])
        elif s[0] == "eob":
            w.code(lc[256], lit_lens[256])
        else:
            _, l, lb, lv, d, db, dv = s
            w.code(lc[l], lit_lens[l]); w.put(lv, lb)
            w.code(dc[d], dist_lens[d]); w.put(dv, db)


# ------------------------------------------------------- hand-made cases ---
def hand_made():
    cases = []

    # empty input payloads
    w = Bits(); stored(w, b"", 1); cases.append(("empty-stored", w.bytes(), {"empty"}))
    w = Bits(); w.put(1, 1); w.put(1, 2); fixed_sym(w, 256); cases.append(("empty-fixed", w.bytes(), {"empty"}))

    # several stored blocks, including empty ones, then a fixed one
    w = Bits()
    stored(w, b"stored one, ", 0); stored(w, b"", 0); stored(w, bytes(range(256)), 0)
    w.put(1, 1); w.put(1, 2)
    for b in b"fixed tail":
        fixed_sym(w, b)
    fixed_sym(w, 256)
    cases.append(("stored-multi-fixed", w.bytes(), {"stored", "multi", "fixed"}))

    # fixed: overlapping copies of every distance 1..8 with every length 3..20
    w = Bits(); w.put(1, 1); w.put(1, 2)
    for b in b"abcdefgh":
        fixed_sym(w, b)
    for d in range(1, 9):
        for L in list(range(3, 21)) + [257, 258]:
            fixed_match(w, L, d)
    fixed_sym(w, 256)
    cases.append(("fixed-overlap-all", w.bytes(), {"fixed", "overlap"}))

    # dynamic: a single-code distance tree (one code of length 1: incomplete,
    # allowed) and a literal tree with only 'a', 'b' and end of block
    lit = [0] * 257; lit[ord("a")] = 1; lit[ord("b")] = 2; lit[256] = 2
    dist = [1]
    w = Bits()
    dynamic_block(w, 1, lit, dist, [("lit", 97), ("lit", 98), ("lit", 97), ("eob",)])
    cases.append(("dynamic-single-dist", w.bytes(), {"dynamic"}))

    # dynamic: complete 286-symbol literal/length and 30-symbol distance
    # alphabets, sent plainly and with repeat codes 16/17/18; two blocks
    lit = [8] * 226 + [9] * 60            # 226/256 + 60/512 = 1
    dist = [4] * 2 + [5] * 28             # 2/16 + 28/32 = 1
    lc = canonical(lit)
    w = Bits()
    syms = [("lit", b) for b in b"dynamic repeat codes "] + [("match", 262, 0, 0, 0, 0, 0), ("eob",)]
    dynamic_block(w, 0, lit, dist, syms)
    sparse = [0] * 286; sparse[256] = 1      # end of block: 1/2
    for b in list(range(97, 97 + 26)) + [32, 46, 44, 10, 58, 59]:
        sparse[b] = 6                         # 32 codes of length 6: 1/2
    dist2 = [0] * 30; dist2[0] = 1; dist2[3] = 1
    syms = [("lit", b) for b in b"sparse.tree, "] + [("match", 257, 0, 0, 3, 0, 0), ("eob",)]
    dynamic_block(w, 1, sparse, dist2, syms, rle=True)
    cases.append(("dynamic-alphabets-rle", w.bytes(), {"dynamic", "multi"}))

    # ------------------------------------------------ malformed (zlib rejects)
    w = Bits(); w.put(1, 1); w.put(3, 2); cases.append(("bad-block-type", w.bytes() + b"\0\0", {"malformed"}))
    w = Bits(); w.put(1, 1); w.put(0, 2); w.align(); w.put(5, 16); w.put(5, 16)
    cases.append(("bad-stored-length", w.bytes() + b"hello", {"malformed"}))
    w = Bits(); w.put(1, 1); w.put(1, 2); fixed_sym(w, 97); fixed_sym(w, 257); w.code(5, 5); w.put(0, 1)
    fixed_sym(w, 256); cases.append(("bad-distance-too-far", w.bytes(), {"malformed"}))
    w = Bits(); w.put(1, 1); w.put(1, 2); fixed_sym(w, 97); fixed_sym(w, 286)
    cases.append(("bad-literal-286", w.bytes() + b"\0", {"malformed"}))
    w = Bits(); w.put(1, 1); w.put(1, 2); fixed_sym(w, 97); fixed_sym(w, 257); w.code(30, 5)
    cases.append(("bad-distance-30", w.bytes() + b"\0", {"malformed"}))
    w = Bits(); w.put(1, 1); w.put(2, 2); w.put(30, 5); w.put(0, 5); w.put(15, 4)
    cases.append(("bad-hlit-287", w.bytes() + b"\0" * 16, {"malformed"}))
    w = Bits(); w.put(1, 1); w.put(2, 2); w.put(0, 5); w.put(30, 5); w.put(15, 4)
    cases.append(("bad-hdist-31", w.bytes() + b"\0" * 16, {"malformed"}))
    # code-length code over-subscribed (all lengths 1)
    w = Bits(); w.put(1, 1); w.put(2, 2); w.put(0, 5); w.put(0, 5); w.put(15, 4)
    for _ in range(19):
        w.put(1, 3)
    cases.append(("bad-cl-oversubscribed", w.bytes() + b"\0" * 64, {"malformed"}))
    # code-length code incomplete (two codes of length 2)
    w = Bits(); w.put(1, 1); w.put(2, 2); w.put(0, 5); w.put(0, 5); w.put(15, 4)
    cl = [0] * 19; cl[8] = 2; cl[0] = 2
    for s in CL_ORDER:
        w.put(cl[s], 3)
    cases.append(("bad-cl-incomplete", w.bytes() + b"\0" * 64, {"malformed"}))
    # repeat code 16 as the first length
    lit = [8] * 257; dist = [1]
    w = Bits(); dynamic_header(w, 1, lit, dist, ops=[(16, 2, 0)] + [(8, 0, 0)] * 255 + [(1, 0, 0)])
    cases.append(("bad-repeat-first", w.bytes() + b"\0" * 8, {"malformed"}))
    # a repeat running past the end of the lengths
    w = Bits(); dynamic_header(w, 1, lit, dist, ops=[(8, 0, 0)] * 255 + [(18, 7, 127)])
    cases.append(("bad-repeat-overflow", w.bytes() + b"\0" * 8, {"malformed"}))
    # no code for end of block (length of 256 is 0)
    lit = [9] * 256 + [0]; lit[0] = 0; lit[1] = 0          # 254/512 incomplete anyway; zlib checks 256 first
    w = Bits(); dynamic_header(w, 1, lit, [1])
    cases.append(("bad-no-end-code", w.bytes() + b"\0" * 8, {"malformed"}))
    # an over-subscribed literal/length code
    lit = [1] * 3 + [0] * 253 + [1]
    w = Bits(); dynamic_header(w, 1, lit, [1])
    cases.append(("bad-lit-oversubscribed", w.bytes() + b"\0" * 8, {"malformed"}))
    # an incomplete literal/length code with more than one code (not allowed)
    lit = [0] * 257; lit[97] = 2; lit[256] = 2
    w = Bits(); dynamic_block(w, 1, lit, [1], [("lit", 97), ("eob",)])
    cases.append(("bad-lit-incomplete", w.bytes(), {"malformed"}))
    # completely empty input: truncated
    cases.append(("empty-input", b"", {"truncated"}))
    return cases


# --------------------------------------------------------- zlib streams ---
def zlib_streams():
    t2k, t9k, t20k = text(2000, 1), text(9000, 2), text(20000, 3)
    n3k = noise(3000, 4)
    cases = [
        ("empty-zlib-l6", deflate(b"", 6), {"empty"}),
        ("empty-zlib-l0", deflate(b"", 0), {"empty"}),
        ("stored-text", deflate(t2k, 0), {"stored"}),
        ("stored-flushes", deflate(t2k, 0, flushes=[(100, zlib.Z_FULL_FLUSH), (1000, zlib.Z_SYNC_FLUSH)]), {"stored", "multi"}),
        ("fixed-text", deflate(t2k, 9, strategy=zlib.Z_FIXED), {"fixed"}),
        ("fixed-rle", deflate(b"ab" * 900 + b"c" * 700, 9, strategy=zlib.Z_FIXED), {"fixed", "overlap"}),
        ("dynamic-text", deflate(t9k, 9), {"dynamic"}),
        ("dynamic-huffman-only", deflate(t2k, 9, strategy=zlib.Z_HUFFMAN_ONLY), {"dynamic"}),
        ("dynamic-rle", deflate(text(3000, 5), 9, strategy=zlib.Z_RLE), {"dynamic", "overlap"}),
        ("multi-memlevel1", deflate(t9k, 6, mem=1), {"dynamic", "multi"}),
        ("multi-flush-mix", deflate(t9k, 6, flushes=[(500, zlib.Z_SYNC_FLUSH), (501, zlib.Z_FULL_FLUSH),
                                                    (3000, zlib.Z_SYNC_FLUSH), (3001, zlib.Z_SYNC_FLUSH)]), {"multi", "stored", "dynamic"}),
        ("multi-wbits9", deflate(t9k, 9, wbits=-9, mem=2), {"dynamic", "multi"}),
        ("compressible-a", deflate(b"a" * 20000, 9), {"compressible", "overlap"}),
        ("compressible-pattern", deflate(b"0123456789" * 1500, 9), {"compressible", "overlap"}),
        ("incompressible-noise", deflate(n3k, 9), {"incompressible", "stored"}),
        ("incompressible-fixed", deflate(noise(1500, 6), 9, strategy=zlib.Z_FIXED), {"incompressible", "fixed"}),
        ("mixed-noise-text", deflate(n3k[:1200] + t2k + n3k[1200:], 6, mem=2), {"multi", "stored", "dynamic"}),
        ("large-text", deflate(t20k, 6), {"dynamic", "large"}),
        ("all-bytes", deflate(bytes(range(256)) * 8, 9), {"dynamic"}),
    ]
    return cases


def beyond_window():
    """a valid stream needing more than the 1,024-byte window (zlib wbits 15):
    the expected result is the documented resource limit 'window"""
    r = random.Random(7)
    block = noise(1500, 8)
    data = block + text(500, 9) + block                 # a 2,000-byte back-reference
    return [("beyond-window", deflate(data, 9, wbits=-15), {"window"})]


def corruptions(cases, count, seed):
    """single and double bit flips in small valid streams (output <= 1,024
    bytes, so no distance can exceed the window)"""
    r = random.Random(seed)
    small = [c for c in cases if 0 < len(c[1]) and "malformed" not in c[2]
             and len(zlib.decompress(c[1], -15)) <= 1024]
    out = []
    for i in range(count):
        name, data, _ = small[i % len(small)]
        b = bytearray(data)
        for _ in range(1 + (i % 2)):
            k = r.randrange(len(b) * 8)
            b[k // 8] ^= 1 << (k % 8)
        out.append(("flip-%03d-%s" % (i, name), bytes(b), {"corrupt"}))
    return out


def fixtures():
    base = hand_made() + zlib_streams()
    small = [("small-dynamic", deflate(text(700, 11), 9), {"dynamic"}),
             ("small-fixed", deflate(text(600, 12), 9, strategy=zlib.Z_FIXED), {"fixed"}),
             ("small-stored", deflate(noise(300, 13), 9), {"stored"})]
    return base + small + beyond_window() + corruptions(base + small, 120, 99)
