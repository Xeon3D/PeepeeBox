"""Draw the toolbar icons this fork needs and write them as 32bpp ICOs.

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


def modem(n):
    """An external modem: a low box with a row of indicators and a phone cord."""
    px = blank(n)
    case = (170, 196, 208, 255)     # B,G,R,A -- the warm beige plastic of the era
    edge = (60, 62, 70, 255)        # dark enough to hold up on a light toolbar
    dark = (120, 142, 152, 255)     # an unlit lamp
    lit = (80, 225, 110, 255)       # the carrier-detect green
    cord = (150, 152, 158, 255)

    y0, y1 = int(n * 0.34), int(n * 0.72)
    x0, x1 = int(n * 0.09), int(n * 0.91)

    # the telephone cord, leaving the back of the case for the wall
    steps = max(2, int(n * 0.20))
    for t in range(steps + 1):
        x = int(n * 0.60 + t)
        y = y0 - 1 - t
        if (0 <= x < n) and (0 <= y < y0):
            px[y][x] = cord

    # the case
    for y in range(y0, y1 + 1):
        hline(px, y, x0, x1, case if (y0 < y < y1) else edge)
    vline(px, x0, y0, y1, edge)
    vline(px, x1, y0, y1, edge)

    # the indicator row, one lamp lit
    ly = (y0 + y1) // 2
    step = max(2, n // 7)
    wide = max(0, (n // 10) - 1)
    for i, x in enumerate(range(x0 + step, x1 - step + 1, step)):
        hline(px, ly, x, x + wide, lit if i == 1 else dark)

    return px


def funlink(n):
    """Two cabinets on one bus: the fun.link adapter."""
    px = blank(n)
    case = (150, 120, 60, 255)      # B,G,R,A -- the fun.link box's blue
    edge = (100, 70, 25, 255)
    glass = (235, 225, 190, 255)    # a lit screen
    wire = (70, 175, 215, 255)      # the loom between them

    w = max(3, int(n * 0.34))
    top = int(n * 0.14)
    bot = int(n * 0.86)
    mid = (top + bot) // 2

    # the cable joining the two, drawn first so the cases sit on top of it
    for y in (mid, mid + 1) if n > 16 else (mid,):
        hline(px, y, 0, n - 1, wire)

    for x0 in (0, n - w):
        x1 = x0 + w - 1
        for y in range(top, bot + 1):
            hline(px, y, x0, x1, case if (top < y < bot) else edge)
        vline(px, x0, top, bot, edge)
        vline(px, x1, top, bot, edge)
        # the screen
        for y in range(top + max(1, n // 12), bot - max(1, n // 8)):
            hline(px, y, x0 + max(1, n // 14), x1 - max(1, n // 14), glass)

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
write_ico(os.path.join(OUT, 'modem.ico'), modem)
write_ico(os.path.join(OUT, 'funlink.ico'), funlink)
