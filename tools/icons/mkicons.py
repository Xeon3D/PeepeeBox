"""Build PeepeeBox's icons.

Two jobs:

  * the four 86Box-*.ico files carry the upstream logo -- a stylised "86" at the small
    sizes and a PC illustration at the large ones.  They are replaced with the Photo Play
    cabinet artwork, at every size each file holds.  All four keep their names so the .rc,
    the .qrc and EMU_ICON_PATH all pick it up without a code change; upstream used the
    colour to signal the build channel, which this fork does not have.

  * there was no dongle icon, and the toolbar's Dongle button borrowed the settings gear.
    One is drawn here: a parallel-port plug in the cabinet's own dark green with the
    amber of its PHOTO PLAY badge, simplified as it gets smaller so it still reads at 16.

  * ppfix gets the cabinet too, with a hammer laid over the corner -- it repairs images,
    and at a glance that has to be what it says.  The hammer is drawn large and haloed,
    because a badge that reads at 16 pixels has to be bold enough to survive the corner
    it sits in.

Entries are written as 32-bit DIBs up to 64 and PNG above, which is what Windows and Qt
both handle without argument.
"""
import io
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFilter

SRC      = 'photoplay_icons/photoplay_%dx%d.ico'
DST      = 'src/qt/icons'
DST_PPFIX = 'tools/ppfix/ppfix.ico'

# straight off the cabinet artwork
GREEN_DARK = (18, 46, 38)
GREEN      = (30, 74, 60)
GREEN_LIT  = (46, 104, 84)
AMBER      = (232, 137, 26)
AMBER_DARK = (150, 78, 10)
STEEL      = (186, 194, 198)
STEEL_DARK = (108, 118, 124)
LED        = (63, 207, 106)
OUTLINE    = (10, 24, 20)
WOOD       = (176, 112, 56)
WOOD_DARK  = (112, 66, 28)
HALO       = (246, 248, 248)


def ico_read(path):
    """Every photoplay_*.ico holds one image, PNG-compressed."""
    b = open(path, 'rb').read()
    size, off = struct.unpack('<II', b[14:22])
    return Image.open(io.BytesIO(b[off:off + size])).convert('RGBA')


def dib(img):
    w, h = img.size
    hdr = struct.pack('<IiiHHIIiiII', 40, w, h * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    px  = img.tobytes()
    rows = []
    for y in range(h - 1, -1, -1):
        r = px[y * w * 4:(y + 1) * w * 4]
        rows.append(bytes(v for i in range(0, len(r), 4)
                          for v in (r[i + 2], r[i + 1], r[i], r[i + 3])))
    stride = ((w + 31) // 32) * 4
    return hdr + b''.join(rows) + (b'\x00' * (stride * h))


def ico_write(path, images):
    ents, datas = [], []
    for img in images:
        w, h = img.size
        if w <= 64:
            d = dib(img)
        else:
            bio = io.BytesIO()
            img.save(bio, 'PNG')
            d = bio.getvalue()
        ents.append((w, h))
        datas.append(d)
    off = 6 + 16 * len(ents)
    out = struct.pack('<HHH', 0, 1, len(ents))
    for (w, h), d in zip(ents, datas):
        out += struct.pack('<BBBBHHII', w & 0xFF, h & 0xFF, 0, 0, 1, 32, len(d), off)
        off += len(d)
    open(path, 'wb').write(out + b''.join(datas))
    return len(ents), off


def dongle(size):
    """A parallel-port dongle, drawn for one size.

    Supersampled, so the shapes stay honest, but the detail is dropped as it shrinks:
    below 24 pixels the pins and the badge ticks turn to mud and cost more than the
    silhouette gains."""
    S = size * 8
    im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d  = ImageDraw.Draw(im)

    def R(*f):
        return [x * S for x in f]

    # Below 24 pixels a faithfully proportioned plug just reads as a green brick, so the
    # connector shell is given more of the width -- the silhouette is all that survives.
    small = size < 24
    x0    = 0.44 if small else 0.36
    body  = 0.40 if small else 0.30

    shell = [(0.02 * S, 0.32 * S), (x0 * S, 0.24 * S),
             (x0 * S, 0.76 * S), (0.02 * S, 0.68 * S)]
    d.polygon(shell, fill=STEEL, outline=OUTLINE, width=max(1, S // 64))

    # the body
    d.rounded_rectangle(R(body, 0.18, 0.96, 0.82), radius=0.10 * S,
                        fill=GREEN, outline=OUTLINE, width=max(1, S // 64))
    # a lit top edge, so it does not read as a flat block
    d.rounded_rectangle(R(body + 0.06, 0.24, 0.92, 0.42), radius=0.06 * S, fill=GREEN_LIT)
    d.rounded_rectangle(R(body + 0.04, 0.54, 0.92, 0.76), radius=0.06 * S, fill=GREEN_DARK)

    # the amber badge the cabinet wears: the one thing that still carries at 16
    d.rounded_rectangle(R(body + 0.14, 0.30, 0.88, 0.46), radius=0.03 * S,
                        fill=AMBER, outline=AMBER_DARK, width=max(1, S // 96))
    if size >= 32:
        for i in range(3):
            x = (0.50 + (i * 0.11)) * S
            d.rectangle([x, 0.34 * S, x + (0.045 * S), 0.42 * S], fill=AMBER_DARK)
        # the pins
        for row, y in ((4, 0.46), (3, 0.56)):
            for i in range(row):
                x = (0.10 + (i * 0.06)) * S
                d.ellipse([x, y * S, x + (0.035 * S), (y + 0.035) * S], fill=STEEL_DARK)
        d.ellipse(R(0.80, 0.60, 0.88, 0.68), fill=LED, outline=OUTLINE,
                  width=max(1, S // 96))
    elif size >= 20:
        d.ellipse(R(0.78, 0.58, 0.90, 0.70), fill=LED)

    return im.resize((size, size), Image.LANCZOS)


def hammer(size, frac=0.60):
    """A claw hammer, for the corner of the ppfix icon.

    Drawn upright and then turned, which keeps the geometry readable; supersampled and
    given a light halo over a dark edge, so it separates from both the cabinet's dark
    green and the transparency around it."""
    n   = max(8, int(round(size * frac)))
    S   = min(max(64, n * 8), 512)
    f   = S / float(n)
    box = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d   = ImageDraw.Draw(box)
    w   = max(1, int(round(0.7 * f)))

    def R(*f):
        return [x * S for x in f]

    # the handle first, so the head sits over its top
    d.rounded_rectangle(R(0.45, 0.28, 0.59, 0.98), radius=0.05 * S,
                        fill=WOOD, outline=OUTLINE, width=w)
    d.rounded_rectangle(R(0.47, 0.38, 0.54, 0.94), radius=0.03 * S, fill=WOOD_DARK)
    # the head: a flat face on the right, a two-prong claw on the left.  Flat and wide,
    # or at a dozen pixels it turns into a bolt.
    d.polygon([R(0.20, 0.12)[0], R(0, 0.12)[1], R(0.02, 0.15)[0], R(0, 0.15)[1],
               R(0.09, 0.22)[0], R(0, 0.22)[1], R(0.02, 0.30)[0], R(0, 0.30)[1],
               R(0.20, 0.33)[0], R(0, 0.33)[1]],
              fill=STEEL, outline=OUTLINE, width=w)
    d.rounded_rectangle(R(0.16, 0.09, 0.94, 0.34), radius=0.04 * S,
                        fill=STEEL, outline=OUTLINE, width=w)
    d.rounded_rectangle(R(0.74, 0.12, 0.91, 0.31), radius=0.03 * S, fill=STEEL_DARK)

    box = box.rotate(-24, resample=Image.BICUBIC)

    # a light halo over a dark edge: the badge has to hold its shape against the cabinet
    a    = box.split()[3]
    rd   = max(1, int(round(0.8 * f)))
    rh   = max(2, int(round(1.1 * f)))
    dark = Image.new('RGBA', box.size, OUTLINE + (255,))
    dark.putalpha(a.filter(ImageFilter.MaxFilter((rd * 2) + 1)))
    halo = Image.new('RGBA', box.size, HALO + (255,))
    halo.putalpha(a.filter(ImageFilter.MaxFilter((rh * 2) + 1)))

    out = Image.alpha_composite(Image.alpha_composite(halo, dark), box)
    return out.resize((n, n), Image.LANCZOS)


def ppfix_icon(art, size):
    """The cabinet with a hammer over its lower-right corner -- or, once there are too
    few pixels for both to survive, the hammer on its own."""
    if size < 32:
        im = Image.new('RGBA', (size, size), (0, 0, 0, 0))
        h  = hammer(size, 1.0)
        im.alpha_composite(h, ((size - h.width) // 2, (size - h.height) // 2))
        return im

    im = art.copy()
    h  = hammer(size, 0.58)
    im.alpha_composite(h, (size - h.width, size - h.height))
    return im


def main():
    sizes = []
    for n in (16, 20, 24, 32, 48, 64, 128, 256):
        if os.path.exists(SRC % (n, n)):
            sizes.append(n)

    pp = [ico_read(SRC % (n, n)) for n in sizes]
    for name in ('86Box-gray', '86Box-green', '86Box-red', '86Box-yellow'):
        cnt, sz = ico_write(os.path.join(DST, name + '.ico'), pp)
        print('%-16s %d images, %d bytes' % (name + '.ico', cnt, sz))

    dg = [dongle(n) for n in sizes]
    cnt, sz = ico_write(os.path.join(DST, 'dongle.ico'), dg)
    print('%-16s %d images, %d bytes' % ('dongle.ico', cnt, sz))

    px = [ppfix_icon(a, n) for a, n in zip(pp, sizes)]
    cnt, sz = ico_write(DST_PPFIX, px)
    print('%-16s %d images, %d bytes' % ('ppfix.ico', cnt, sz))

    if '--preview' in sys.argv:
        sheet = Image.new('RGBA', (660, 290), (45, 45, 45, 255))
        for row, imgs in enumerate((dg, px)):
            x = 8
            for img in imgs:
                sheet.paste(img, (x, 24 + (row * 140)), img)
                big = img.resize((72, 72), Image.NEAREST)
                sheet.paste(big, (x, 60 + (row * 140)), big)
                x += 80
        sheet.save(sys.argv[sys.argv.index('--preview') + 1])


main()
