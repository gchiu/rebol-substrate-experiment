import struct, sys, zlib
p = sys.argv[1] if len(sys.argv) > 1 else "glon-paint.png"
b = open(p, "rb").read()
assert b[:8] == b"\x89PNG\r\n\x1a\n", "not PNG"
pos, w, h, idat = 8, None, None, b""
while pos < len(b):
    n = struct.unpack(">I", b[pos:pos+4])[0]; typ = b[pos+4:pos+8]; data = b[pos+8:pos+8+n]; pos += 12+n
    if typ == b"IHDR": w, h = struct.unpack(">II", data[:8])
    elif typ == b"IDAT": idat += data
    elif typ == b"IEND": break
assert w == 800 and h == 600 and idat, (w,h)
raw = zlib.decompress(idat)
assert len(raw) == (w*4+1)*h
# Smoke tests draw at least one stroke; reject a blank canvas.
stride = w * 4
prev = bytearray(stride)
off = 0
nonwhite = 0
for y in range(h):
    f = raw[off]; off += 1
    cur = bytearray(raw[off:off+stride]); off += stride
    for i in range(stride):
        a = cur[i-4] if i >= 4 else 0
        up = prev[i]
        ul = prev[i-4] if i >= 4 else 0
        if f == 1: cur[i] = (cur[i] + a) & 255
        elif f == 2: cur[i] = (cur[i] + up) & 255
        elif f == 3: cur[i] = (cur[i] + ((a + up) // 2)) & 255
        elif f == 4:
            pp = a + up - ul
            pa, pb, pc = abs(pp-a), abs(pp-up), abs(pp-ul)
            pr = a if pa <= pb and pa <= pc else (up if pb <= pc else ul)
            cur[i] = (cur[i] + pr) & 255
    for i in range(0, stride, 4):
        if tuple(cur[i:i+3]) != (255, 255, 255): nonwhite += 1
    prev = cur
assert nonwhite > 0, "blank PNG"
print("PNG OK: %dx%d %d bytes, %d non-white px" % (w, h, len(b), nonwhite))
