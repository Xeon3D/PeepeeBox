"""Draw the two toolbar icons this fork needs and write them as 32bpp ICOs.

16x16 and 32x32, BGRA, bottom-up DIB inside an ICO container -- the same shape
the rest of src/qt/icons uses, and small enough to keep in the repository.
"""
import struct, os, math

OUT = r'C:\Users\xeon4\Documents\Claude\PeepeeBox dev\src\qt\icons'


def blank(n):
    return [[(0, 0, 0, 0) for _ in range(n)] for _ in range(n)]


def disc(px, cx, cy, r, col, edge=None):
    n = len(px)
    for y in range(n):
        for x in range(n):
            d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
            if d <= r:
                px[y][x] = col
            elif edge is not None and d <= r + 0.9:
                px[y][x] = edge


def hline(px, y, x0, x1, col):
    for x in range(int(x0), int(x1) + 1):
        if 0 <= y < len(px) and 0 <= x < len(px):
            px[y][x] = col


def vline(px, x, y0, y1, col):
    for y in range(int(y0), int(y1) + 1):
        if 0 <= y < len(px) and 0 <= x < len(px):
            px[y][x] = col


def coin(n):
    """A coin dropping into a slot."""
    px = blank(n)
    gold = (60, 175, 225, 255)      # B,G,R,A -- warm gold
    dark = (20, 110, 160, 255)
    slot = (90, 90, 90, 255)
    r = n * 0.30
    disc(px, n * 0.42, n * 0.40, r, gold, dark)
    # the milled edge: a lighter arc on the upper left
    for a in range(150, 260, 4):
        x = n * 0.42 + (r - 0.8) * math.cos(math.radians(a))
        y = n * 0.40 + (r - 0.8) * math.sin(math.radians(a))
        if 0 <= int(y) < n and 0 <= int(x) < n:
            px[int(y)][int(x)] = (140, 220, 250, 255)
    # the slot it goes into
    y0 = int(n * 0.78)
    for t in range(max(1, n // 12)):
        hline(px, y0 + t, n * 0.16, n * 0.84, slot)
    return px


def crosshair(n):
    """A calibration target."""
    px = blank(n)
    ink = (70, 70, 70, 255)
    hot = (60, 60, 220, 255)
    cx = cy = n / 2.0
    r = n * 0.34
    for a in range(0, 360, 3):
        x = cx + r * math.cos(math.radians(a))
        y = cy + r * math.sin(math.radians(a))
        if 0 <= int(y) < n and 0 <= int(x) < n:
            px[int(y)][int(x)] = ink
    hline(px, int(cy), 0, n * 0.22, ink)
    hline(px, int(cy), n * 0.78, n - 1, ink)
    vline(px, int(cx), 0, n * 0.22, ink)
    vline(px, int(cx), n * 0.78, n - 1, ink)
    disc(px, cx + 0.5, cy + 0.5, max(1.2, n * 0.09), hot)
    return px


def dib(px):
    n = len(px)
    hdr = struct.pack('<IiiHHIIiiII', 40, n, n * 2, 1, 32, 0, n * n * 4, 0, 0, 0, 0)
    body = b''
    for y in range(n - 1, -1, -1):
        for x in range(n):
            body += bytes(px[y][x])
    mask = b'\x00' * (n * 4)          # 1bpp AND mask, padded to 32-bit rows
    return hdr + body + mask


def write_ico(path, draw):
    imgs = [dib(draw(s)) for s in (16, 32)]
    out = struct.pack('<HHH', 0, 1, len(imgs))
    off = 6 + 16 * len(imgs)
    for size, blobb in zip((16, 32), imgs):
        out += struct.pack('<BBBBHHII', size, size, 0, 0, 1, 32, len(blobb), off)
        off += len(blobb)
    out += b''.join(imgs)
    with open(path, 'wb') as f:
        f.write(out)
    print("wrote %s  %d bytes" % (os.path.basename(path), len(out)))


write_ico(os.path.join(OUT, 'coin.ico'), coin)
write_ico(os.path.join(OUT, 'calibrate.ico'), crosshair)
