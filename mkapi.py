#!/usr/bin/env python3
"""Write api/zxdesk.inc, the include an app is assembled against.

    ./build.sh && ./mkapi.py

Reads build/zxdesk.sym. The slots are the jump table at $8000, so the
addresses hold for every build of the same interface version.
"""
import sys

PREFIXES = ("KEY_", "ST_", "STCAP_", "STERR_", "FA_", "APP_")
SINGLES = ("API_VER", "API_SLOTS", "APPSIZE", "WinCapH")


def symbols(path):
    out = {}
    for line in open(path):
        p = line.split()
        if len(p) >= 3 and p[1].upper() == "EQU":
            try:
                out[p[0]] = int(p[2].rstrip("Hh"), 16)
            except ValueError:
                pass
    return out


def render(syms):
    end = syms["ApiEnd"]
    slots = sorted((v, k) for k, v in syms.items()
                   if k.startswith("Api") and 0x8003 <= v < end
                   and (v - 0x8000) % 3 == 0)
    lines = ["; ZX Desk application interface, version %d." % syms["API_VER"],
             "; Written by mkapi.py. Do not edit.",
             "; Slot n is a jump at $8000 + 3n. Arguments are in src/api.inc.",
             ""]
    for v, k in slots:
        lines.append("%-16sequ     $%04X   ; %d" % (k, v, (v - 0x8000) // 3))
    lines.append("")
    consts = sorted((k, v) for k, v in syms.items()
                    if k.startswith(PREFIXES) or k in SINGLES)
    for k, v in consts:
        lines.append("%-16sequ     %d" % (k, v))
    return "\n".join(lines) + "\n"


def main():
    sym = sys.argv[1] if len(sys.argv) > 1 else "build/zxdesk.sym"
    out = sys.argv[2] if len(sys.argv) > 2 else "api/zxdesk.inc"
    text = render(symbols(sym))
    open(out, "w").write(text)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
