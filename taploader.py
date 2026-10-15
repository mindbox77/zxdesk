#!/usr/bin/env python3
"""Give a pasmo --tapbas tape a loader that reads two blocks.

    ./taploader.py build/zxdesk.tap src/wallpaper.bin

pasmo's BASIC loader is CLEAR, POKE 23610,255, one LOAD ""CODE and
RANDOMIZE USR 32768. The desktop now has a second block, the
wallpaper band, loaded into the screen before the jump and moved
into bank 1 by the program on a 128K. The first two blocks
of the tape are replaced with a loader that does that, the code
block is kept as pasmo wrote it, and the band goes on the end as a
CODE file named "wallpaper". Everything the tape backend plants
after the program with taplant.py still comes after it.
"""
import sys

WALLLOAD = 0x4000
START = 32768

def block(flag, payload):
    body = bytes([flag]) + payload
    chk = 0
    for b in body:
        chk ^= b
    body += bytes([chk])
    return len(body).to_bytes(2, "little") + body

def header(kind, name, length, p1, p2):
    n = name.ljust(10)[:10].encode("ascii")
    return block(0x00, bytes([kind]) + n
                 + length.to_bytes(2, "little")
                 + p1.to_bytes(2, "little")
                 + p2.to_bytes(2, "little"))

def num(n):
    # a number as BASIC stores it: the digits, then the five byte form
    return str(n).encode("ascii") + bytes([0x0E, 0, 0, n & 255, n >> 8, 0])

def line(number, body):
    return number.to_bytes(2, "big") + (len(body) + 1).to_bytes(2, "little") + body + b"\r"

CLEAR, POKE, LOAD, CODE, RANDOMIZE, USR = 0xFD, 0xF4, 0xEF, 0xAF, 0xF9, 0xC0

def loader():
    prog = (line(10, bytes([CLEAR]) + num(START - 8193))
            + line(20, bytes([POKE]) + num(23610) + b"," + num(255))
            + line(30, bytes([LOAD]) + b'""' + bytes([CODE]))
            + line(40, bytes([LOAD]) + b'""' + bytes([CODE]))
            + line(50, bytes([RANDOMIZE, USR]) + num(START)))
    return header(0, "loader", len(prog), 10, len(prog)) + block(0xFF, prog)

def blocks(data):
    i = 0
    while i < len(data):
        n = int.from_bytes(data[i:i + 2], "little")
        yield data[i:i + 2 + n]
        i += 2 + n

def main():
    tap, band = sys.argv[1], sys.argv[2]
    parts = list(blocks(open(tap, "rb").read()))
    if len(parts) < 4 or parts[1][2] != 0xFF or parts[0][3] != 0:
        sys.exit(f"{tap}: not a pasmo --tapbas tape")
    wall = open(band, "rb").read()
    if len(wall) != 4096:
        sys.exit(f"{band}: {len(wall)} bytes, the band is 4096")
    out = loader() + b"".join(parts[2:])
    out += header(3, "wallpaper", len(wall), WALLLOAD, START) + block(0xFF, wall)
    open(tap, "wb").write(out)
    print(f"{tap}: loader for two blocks, wallpaper appended")

main()
