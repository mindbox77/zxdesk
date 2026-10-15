#!/usr/bin/env python3
"""Turn a picture into the wallpaper band ZX Desk keeps at $EF00.

    ./mkwall.py picture.png src/wallpaper.bin [--threshold N] [--invert]

The band is sixteen character rows, pixel rows 32 to 159, stored
linear: thirty two bytes a pixel row, top to bottom, 4,096 bytes.
The picture is scaled to fit 256 by 128 keeping its shape, centred,
and thresholded to one bit: darker than the threshold is ink. Rows
the picture does not reach are blank. A preview is written next to
the output, drawn from the band bytes themselves rather than the
picture, so it shows exactly what the Spectrum will.
"""
import sys
from PIL import Image

W, H = 256, 128
ROWS = 32

def main():
    args = sys.argv[1:]
    if len(args) < 2:
        print(__doc__)
        sys.exit(1)
    src, out = args[0], args[1]
    threshold, invert = 128, False
    i = 2
    while i < len(args):
        if args[i] == "--threshold":
            threshold = int(args[i + 1]); i += 2
        elif args[i] == "--invert":
            invert = True; i += 1
        else:
            print("unknown option", args[i]); sys.exit(1)

    im = Image.open(src).convert("L")
    sw, sh = im.size
    scale = min(W / sw, H / sh)
    nw, nh = max(1, round(sw * scale)), max(1, round(sh * scale))
    im = im.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("L", (W, H), 255)
    ox, oy = (W - nw) // 2, (H - nh) // 2
    canvas.paste(im, (ox, oy))
    px = canvas.load()

    band = bytearray(W // 8 * H)
    for y in range(H):
        for x in range(W):
            ink = px[x, y] < threshold
            if invert:
                ink = not ink
            if ink:
                band[y * ROWS + (x >> 3)] |= 0x80 >> (x & 7)
    open(out, "wb").write(band)

    prev = Image.new("1", (W, H), 1)
    pp = prev.load()
    for y in range(H):
        for x in range(W):
            if band[y * ROWS + (x >> 3)] & (0x80 >> (x & 7)):
                pp[x, y] = 0
    preview = out.rsplit(".", 1)[0] + "-preview.png"
    prev.resize((W * 3, H * 3), Image.NEAREST).save(preview)
    ink = sum(bin(b).count("1") for b in band)
    print(f"{out}: {len(band)} bytes, picture {nw}x{nh} at ({ox},{oy}), "
          f"{ink} ink pixels; preview {preview}")

main()
