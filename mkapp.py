#!/usr/bin/env python3
"""Assemble a ZX Desk app into a loadable file.

    ./mkapp.py examples/counter/counter.asm build/counter.zxa
    ./mkapp.py examples/counter/counter.asm build/counter.zxa \\
        --tap build/zxdesk.tap COUNTER

The source includes "zxdesk.inc", starts with ORG APPORG and puts its
24 byte descriptor first. It is assembled at three origins: two to find
the words that hold addresses, the third to prove the list is complete.

File: 'ZXA', interface version, image length, relocation count, the
image, then one word per relocation: an offset into the image.

--tap appends the file to a tape image as a header and a data block,
so FILE, LOAD with FROM set to TAPE finds it after the desktop.
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.abspath(__file__))


def pasmo():
    local = os.path.join(ROOT, "tools", "bin", "pasmo")
    return local if os.path.exists(local) else "pasmo"


def assemble(src, org, inc):
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        out = f.name
    try:
        r = subprocess.run([pasmo(), "-I", inc, "--equ", f"APPORG={org}",
                            "--bin", src, out],
                           capture_output=True, text=True)
        if r.returncode or "ERROR" in r.stdout + r.stderr:
            sys.exit((r.stdout + r.stderr).strip() or "pasmo failed")
        return open(out, "rb").read()
    finally:
        os.unlink(out)


def api_const(inc, name):
    text = open(os.path.join(inc, "zxdesk.inc")).read()
    return int(re.search(rf"^{name}\s+equ\s+(\d+)", text, re.M).group(1))


def build(src, inc=None):
    """Returns the bytes of the loadable file."""
    inc = inc or os.path.join(ROOT, "api")
    a = assemble(src, 0, inc)
    b = assemble(src, 0x0100, inc)
    c = assemble(src, 0x1234, inc)
    if not (len(a) == len(b) == len(c)):
        sys.exit("the image changes length with its origin")
    if len(a) < api_const(inc, "APPSIZE"):
        sys.exit("shorter than a descriptor")
    relocs = []
    for i in range(1, len(a)):
        if a[i] != b[i]:
            if (b[i] - a[i]) & 0xFF != 1 or a[i - 1] != b[i - 1]:
                sys.exit(f"offset {i}: not a plain address word")
            relocs.append(i - 1)
    image = bytearray(a)
    for off in relocs:
        w = (image[off] | image[off + 1] << 8) + 0x1234
        image[off], image[off + 1] = w & 0xFF, (w >> 8) & 0xFF
    if bytes(image) != c:
        bad = next(i for i in range(len(c)) if image[i] != c[i])
        sys.exit(f"offset {bad}: uses half an address, which cannot be "
                 f"relocated. Load the whole word instead.")
    out = bytearray(b"ZXA")
    out.append(api_const(inc, "API_VER"))
    out += len(a).to_bytes(2, "little")
    out += len(relocs).to_bytes(2, "little")
    out += a
    for off in relocs:
        out += off.to_bytes(2, "little")
    return bytes(out)


def tap_block(flag, payload):
    body = bytes([flag]) + payload
    chk = 0
    for x in body:
        chk ^= x
    body += bytes([chk])
    return len(body).to_bytes(2, "little") + body


def tap_append(tap, name, data):
    n = name.upper().ljust(10)[:10].encode("ascii")
    head = (bytes([3]) + n + len(data).to_bytes(2, "little")
            + (0).to_bytes(2, "little") + (32768).to_bytes(2, "little"))
    with open(tap, "ab") as f:
        f.write(tap_block(0x00, head))
        f.write(tap_block(0xFF, data))


def main():
    args = sys.argv[1:]
    if len(args) not in (2, 5) or (len(args) == 5 and args[2] != "--tap"):
        sys.exit(__doc__)
    data = build(args[0])
    open(args[1], "wb").write(data)
    n = int.from_bytes(data[4:6], "little")
    r = int.from_bytes(data[6:8], "little")
    print(f"{args[1]}: {len(data)} bytes, image {n}, {r} relocations, "
          f"needs interface version {data[3]}")
    if len(args) == 5:
        tap_append(args[3], args[4], data)
        print(f"appended to {args[3]} as {args[4].upper()}")


if __name__ == "__main__":
    main()
