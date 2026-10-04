#!/usr/bin/env python3
"""Run a routine from a ZX Desk build headlessly and assert on memory.

The project instructions asked for this and it had not been built: a
harness that loads the binary, steps the CPU and checks the result,
rather than reading numbers off a screenshot. It does not replace the
Fuse run, because it models the CPU and not the video, has no ROM and
no interrupt source, so anything touching the character set, the tape
ROM routines or frame timing still has to go through Fuse.

What it is good for is the correctness subjects: the editor, the
storage layer and the keyboard decode are pure RAM and can be driven
here in a fraction of a second with a real assertion at the end.

    TEST=1 ./build.sh           assemble what this drives
    ./zxtest.py                 run every headless subject and assert
    ./zxtest.py --shot          boot the desktop and render a PNG
    ./zxtest.py --shots         render the panels and the two windows
"""
import struct
import sys
import zlib

import z80

# The image starts at the slow region now, not at $8000. Loading it at
# $8000 would put every routine 8K high and the first CALL would land
# in the middle of something else.
ORG = 0x6000
ROM = "/Applications/Fuse.app/Contents/Resources/48.rom"
SCREEN = 0x4000
CHARSET = 0x3C00
SENTINEL = 0x0100          # nothing is mapped here; a HALT is planted to land on

# get_state_view() returns the whole machine record, not the memory:
# forty bytes of CPU state and then the 64K image. Reading it as if it
# started at address zero silently returns CPU registers as if they
# were program bytes, which looks exactly like a miscompiled build.
MEMOFF = 40
STACK = 0xBD00


def load_symbols(path):
    syms = {}
    for line in open(path):
        parts = line.split()
        if len(parts) >= 3 and parts[1].upper() == "EQU":
            val = parts[2].rstrip("Hh")
            try:
                syms[parts[0]] = int(val, 16)
            except ValueError:
                pass
    return syms


class Machine:
    def __init__(self, binpath, sympath, banked=False):
        self.m = z80.Z80Machine()
        self.syms = load_symbols(sympath)
        self.mem = self.m.get_state_view()
        # F3. A 128K pages one of eight 16K banks in at $C000, chosen by
        # the low three bits of a write to $7FFD. The emulator has flat
        # memory, so the banks live here and a write swaps the window's
        # contents in and out. Without this the whole banking backend
        # could only be run in Fuse, like the tape one, and a backend
        # that can only be run in Fuse is a backend nothing asserts on.
        self.banked = banked
        self.banks = [bytearray(0x4000) for _ in range(8)]
        self.cur_bank = 0
        self.m.set_output_callback(self._out)
        # A key is down when its bit reads low, and the row is selected
        # by the high byte of the port address.
        self.keys = {}
        self.m.set_input_callback(
            lambda addr: self.keys.get((addr >> 8) & 0xFF, 0xFF)
            if (addr & 0xFF) == 0xFE else 0xFF)
        try:
            self.m.set_memory_block(0x0000, open(ROM, "rb").read())
            self.has_rom = True
        except OSError:
            self.has_rom = False       # the font subjects will be skipped
        code = open(binpath, "rb").read()
        self.m.set_memory_block(ORG, code)
        self.end = ORG + len(code)

    def _out(self, addr, value):
        if not self.banked or (addr & 0x8002) != 0:
            return                          # $7FFD has A15 and A1 low
        want = value & 7
        if want == self.cur_bank:
            return
        off = MEMOFF + 0xC000
        self.banks[self.cur_bank][:] = self.mem[off:off + 0x4000]
        self.m.set_memory_block(0xC000, bytes(self.banks[want]))
        self.cur_bank = want

    def bank_byte(self, bank, offset):
        """Read a byte of a bank that is not currently paged in."""
        if bank == self.cur_bank:
            return self.peek(0xC000 + offset)
        return self.banks[bank][offset]

    def call(self, name, limit=20_000_000):
        """Run the routine at `name` until it returns to the sentinel.

        The sentinel is a HALT rather than a breakpoint. A breakpoint
        did not stop this emulator, and the failure was not obvious:
        execution ran off into unmapped memory, which is all zeros, so
        it slid up a NOP field to $8000 and quietly ran the entire
        program a second time. The results looked like a broken editor
        rather than a broken harness.
        """
        addr = self.syms[name]
        self.poke(SENTINEL, 0x76)              # HALT
        self.m.sp = STACK
        self.m.sp = (self.m.sp - 2) & 0xFFFF
        self.poke16(self.m.sp, SENTINEL)
        self.m.pc = addr
        ticks = 0
        while ticks < limit:
            ticks += self.m.run()
            if self.m.halted:
                if self.m.pc not in (SENTINEL, SENTINEL + 1):
                    raise RuntimeError(
                        f"{name} halted at {self.m.pc:04X}, not the sentinel")
                self.m.halted = False
                return ticks
        raise RuntimeError(f"{name} did not return within {limit} ticks")

    def call_with_a(self, name, value):
        """Same as call(), but with A loaded, which is how NoteKey is entered."""
        self.m.a = value
        return self.call(name)

    def peek(self, addr):
        return self.mem[MEMOFF + addr]

    def peek16(self, addr):
        return self.peek(addr) | (self.peek(addr + 1) << 8)

    def poke(self, addr, val):
        self.m.set_memory_block(addr, bytes([val & 0xFF]))

    def poke16(self, addr, val):
        self.m.set_memory_block(addr, bytes([val & 0xFF, (val >> 8) & 0xFF]))

    def sym(self, name):
        return self.syms[name]

    # ---- the display, so a paint can be asserted rather than looked at ----
    def scr_addr(self, col, pxrow):
        return (SCREEN + ((pxrow & 0xC0) << 5) + ((pxrow & 0x07) << 8)
                + ((pxrow & 0x38) << 2) + col)

    def glyph_at(self, col, pxrow):
        """The eight bytes on screen at a byte column and pixel row."""
        return bytes(self.peek(self.scr_addr(col, pxrow + i)) for i in range(8))

    def font_glyph(self, ch, invert=False):
        base = CHARSET + ord(ch) * 8
        g = bytes(self.peek(base + i) for i in range(8))
        return bytes(b ^ 0xFF for b in g) if invert else g


def write_png(path, pixels, w, h):
    """Minimal greyscale PNG. Avoids a dependency for twenty lines of work."""
    raw = b"".join(b"\x00" + bytes(pixels[y * w:(y + 1) * w]) for y in range(h))

    def chunk(tag, data):
        body = tag + data
        return (struct.pack(">I", len(data)) + body
                + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF))

    open(path, "wb").write(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b""))


def boot(text="ZX DESK NOTES"):
    """Boot the desktop headlessly, exactly in the order Main does."""
    m = Machine("build/zxdesk.bin", "build/zxdesk.sym")
    for r in ("DetectMachine", "SetupIM2", "BuildScrTab", "MouseInit",
              "JoyInit", "StInit", "SetLoad", "SetApply", "DskInit",
              "InitScreen", "WndInit"):
        m.call(r)
    for ch in text:
        m.call_with_a("NoteKey", ord(ch))
    for r in ("WinDraw", "WinGrab", "PtrSaveBg", "PtrDraw"):
        m.call(r)
    return m


def render(m, path):
    px = bytearray(256 * 192)
    for y in range(192):
        for cx in range(32):
            b = m.peek(m.scr_addr(cx, y))
            for bit in range(8):
                px[y * 256 + cx * 8 + bit] = 0 if (b >> (7 - bit)) & 1 else 255
    write_png(path, px, 256, 192)
    print(f"wrote {path}")


def shot(path, text="ZX DESK NOTES"):
    """Boot the desktop headlessly and render the pixel file.

    Not a substitute for the Fuse run, which is the only thing that
    sees the real ULA, but it shows what the code paints without
    needing a window server, and it is the same picture.
    """
    render(boot(text), path)


def shots():
    """The pictures a settings change is supposed to produce.

    The three states worth looking at: the panel itself, the desktop
    with the lattice switched off, and the desktop with it switched
    back on. The last one exists because turning a setting off and on
    again must land back where it started, and a repaint that leaves
    the save under stale would show here as a rectangle of the wrong
    background where the panel was.
    """
    m = boot()
    m.call("DlgOpen")
    render(m, "shots/headless-settings.png")

    # click LATTICE, which is line two, so pixel row DLGROW0 + 16
    m.m.c = m.sym("DLGROW0") + 16
    m.call("DlgClick")
    render(m, "shots/headless-lattice-off.png")
    print(f"  SetLattice is now {m.peek(m.sym('SetLattice'))}")

    m.m.c = m.sym("DLGROW0") + 16
    m.call("DlgClick")
    render(m, "shots/headless-lattice-on.png")
    print(f"  SetLattice is now {m.peek(m.sym('SetLattice'))}")

    # G3's two panels, with something in the directory to list
    m = boot()
    m.call("FmSavePanel")
    render(m, "shots/headless-saveas.png")
    for k in [8] * 12 + list("SPECTRUM48") + [4, 13]:
        m.m.a = k if isinstance(k, int) else ord(k)
        m.call("PnlKey")
    m.call("FmDeletePanel")
    m.m.c = m.sym("DLGROW0")
    m.call("PnlClick")
    render(m, "shots/headless-confirm.png")
    m.m.a = 13
    m.call("DlgKey")
    m.call("FmOpenPanel")
    render(m, "shots/headless-files.png")


def main():
    if "--shot" in sys.argv:
        shot("shots/headless-notepad.png")
        return 0
    if "--shots" in sys.argv:
        shots()
        return 0

    # The correctness subjects, from the TEST build. The timing harness
    # is a separate binary now: it needs an interrupt to anchor to and a
    # screen to print on, neither of which exists here, and the two were
    # competing for the same fifteen kilobytes.
    m = Machine("build/zxtest.bin", "build/zxtest.sym")
    m.call("StInit")
    # F1. WinBufP is no longer an address the assembler filled in, so a
    # harness that skips WndInit has every subject painting into nought.
    # That is not a quiet failure either: the window buffer is 2,304
    # bytes from $0000, which includes the HALT this harness plants at
    # $0100 to catch a return, so the first WinGrab took the sentinel
    # out and the run never came back.
    m.call("BuildScrTab")
    m.call("WndInit")

    failures = []

    # The subjects that are pure RAM. The tape backend is not among
    # them: it calls the ROM loader, which this emulator does not have.
    for routine, counter, total in [
        ("BNoteTest", "BNoteRes", "BNOTEN"),
        ("BKbdTest", "BKbdRes", "BKBDN"),
        ("BSetTest", "BSetRes", "BSETN"),
        # C2 and B4. This one was only ever in the Fuse report, where it
        # read 6 of 13 from B4 until F1 and nobody read the number as a
        # failure. It is a count with a right answer, so it belongs here.
        ("BHitTest", "BHitRes", "BHITN"),
    ]:
        m.call(routine)
        got, want = m.peek(m.sym(counter)), m.sym(total)
        print(f"{routine:12s} {got} of {want}")
        if got != want:
            failures.append(routine)

    failures += blit_checks(m)
    failures += field_checks(m)
    failures += api_checks(m)

    m.call("BStoreTest")
    print(f"BStoreTest   wrote {m.peek16(m.sym('BStWrote'))}, "
          f"read {m.peek16(m.sym('BStRead'))}, "
          f"{m.peek(m.sym('BStBad'))} wrong, err {m.peek(m.sym('BStErr'))}")

    # the editor buffer, because a count alone does not say which
    # check failed
    m.call("BNoteTest")
    base, stride = m.sym("NoteBuf"), m.sym("NOTESTRIDE")
    cols = m.sym("NOTECOLS")
    print("note buffer after the run:")
    for row in range(m.sym("NOTEROWS")):
        text = "".join(chr(m.peek(base + row * stride + c)) for c in range(cols))
        print(f"  {row}: [{text}]")
    print(f"  cursor {m.peek(m.sym('NoteCX'))},{m.peek(m.sym('NoteCY'))}")

    if m.has_rom:
        failures += paint_checks(m)
        failures += settings_checks()
        failures += interrupt_checks()
        failures += watchdog_checks()
        failures += blitclip_checks()
        failures += compositor_checks()
        failures += mouse_checks()
        failures += heap_checks()
        failures += app_checks()
        failures += app_partial_checks()
        failures += desktop_checks()
        failures += commander_checks()
        failures += arrange_checks()
        failures += shortcut_checks()
        failures += printer_checks()
        failures += scrollbar_checks()
        failures += sound_checks()
        failures += close_checks()
        failures += clock_checks()
        failures += calendar_checks()
        failures += bank_checks()
        failures += surface_checks()
        failures += filemgr_checks()
        failures += dialog_checks()
        failures += loader_checks()
    else:
        print("no 48K ROM found, skipping the paint checks")

    if failures:
        print("FAILED:", ", ".join(failures))
        return 1
    print("all headless subjects pass")
    return 0


def api_checks(m):
    """Slot n is at $8000 + 3n. The order is fixed; append only."""
    failures = []
    base = 0x8000
    slots = [
        ("Main", "Start"),
        ("ApiVersion", "ApiVerGet"),
        ("ApiWndOpen", "WndOpen"),
        ("ApiWndClose", "WndClose"),
        ("ApiWndIsFront", "WndIsFront"),
        ("ApiWndRepaint", "WndRepaintAll"),
        ("ApiWndBar", "WndScrollBar"),
        ("ApiWinGrab", "WinGrab"),
        ("ApiPrint", "PrintStrPx2"),
        ("ApiPrintClip", "WinPrintClip"),
        ("ApiFillRect", "DevFillRect"),
        ("ApiAddrAt", "AddrAt"),
        ("ApiPtrRestore", "PtrRestore"),
        ("ApiPtrSaveBg", "PtrSaveBg"),
        ("ApiPtrDraw", "PtrDraw"),
        ("ApiAlloc", "HeapAlloc"),
        ("ApiFree", "HeapFree"),
        ("ApiFreeOwner", "HeapFreeOwner"),
        ("ApiEvPost", "EvPost"),
        ("ApiStSelect", "StSelect"),
        ("ApiStOpen", "StOpen"),
        ("ApiStClose", "StClose"),
        ("ApiStRead", "StRead"),
        ("ApiStWrite", "StWrite"),
        ("ApiStDir", "StDir"),
        ("ApiStDelete", "StDelete"),
        ("ApiStCaps", "StCaps"),
        ("ApiStIdent", "StIdent"),
        # version 2
        ("ApiWinRec", "WinRecGet"),
        ("ApiWinPrint", "WinPrint"),
        ("ApiWinPrintInv", "WinPrintInv"),
        ("ApiWinClear", "WinClear"),
        ("ApiWinAddr", "WinAddr"),
        # version 3
        ("ApiWinRows", "WinRows"),
        ("ApiWinCols", "WinCols"),
        ("ApiWinApp", "WinAppGet"),
        ("ApiWndCloseNow", "WndCloseNow"),
        ("ApiKbdMods", "KbdModsGet"),
        ("ApiWinPrintIf", "WinPrintIf"),
        ("ApiDlgAlert", "DlgAlert"),
        ("ApiDlgConfirm", "DlgConfirm"),
        ("ApiDlgSave3", "DlgSave3"),
        ("ApiOpenFile", "SysOpenFile"),
        ("ApiWinFill", "WinFill"),
        # version 4
        ("ApiWinRefresh", "WinRefresh"),
    ]
    bad = []
    for n, (slot, target) in enumerate(slots):
        at = base + 3 * n
        if (m.sym(slot) != at or m.peek(at) != 0xC3
                or m.peek16(at + 1) != m.sym(target)):
            bad.append(slot)
    m.call("ApiVersion")
    ver, count = m.m.a, m.m.b
    print("application interface:")
    if bad:
        failures.append("api slots: " + ", ".join(bad))
    else:
        print(f"  {len(slots)} slots from ${base:04X}, each a jump to the "
              f"routine it names")
    if (ver, count) != (m.sym("API_VER"), len(slots)):
        failures.append(f"ApiVersion said version {ver}, {count} slots")
    else:
        print(f"  slot 1 reports version {ver} and {count} slots")
    m.call("ApiWinRec")
    if m.m.hl != m.sym("WinRec"):
        failures.append("ApiWinRec did not return the live record")
    else:
        print("  slot 28 returns the live window record")
    if m.sym("ApiEnd") != base + 3 * len(slots):
        failures.append("the table does not end where the list does")
    return failures


def paint_checks(m):
    """Type into the notepad and check what actually reached the screen.

    This is the half the buffer tests cannot reach. Every character is
    compared against the ROM glyph it should be, at the pixel position
    it should be at, so a wrong column or a wrong row fails here rather
    than looking plausible in a screenshot.
    """
    failures = []
    m.call("BuildScrTab")
    m.call("InitScreen")
    m.call("StInit")
    m.call("NoteNew")
    for ch in "HELLO":
        m.call_with_a("NoteKey", ord(ch))
    m.call("WinDraw")

    winx, winy = m.peek(m.sym("WinX")), m.peek(m.sym("WinY"))
    col0 = winx + m.sym("NOTEIND")
    row0 = winy + m.sym("NOTETOP")

    print("paint: row 0 of the window, glyph by glyph")
    expected = "HELLO" + " " * (m.sym("NOTECOLS") - 5)
    for i, ch in enumerate(expected):
        got = m.glyph_at(col0 + i, row0)
        if i == 5:
            want = m.font_glyph(" ", invert=True)   # the cursor sits here
            label = "cursor"
        else:
            want = m.font_glyph(ch)
            label = repr(ch)
        if got != want:
            failures.append(f"paint col {i} ({label})")
            print(f"  col {i:2d} {label:8s} MISMATCH got {got.hex()} want {want.hex()}")
    if not failures:
        print(f"  all {len(expected)} cells match the ROM glyphs, "
              f"cursor inverted at column 5")

    # The signature element: the cursor reports the shift state, so
    # SYMBOL SHIFT's invisible punctuation is discoverable.
    for mods, letter in ((1, "C"), (2, "S")):
        m.poke(m.sym("KbdModState"), mods)
        m.call("NoteCurDraw")
        got = m.glyph_at(col0 + 5, row0)
        want = m.font_glyph(letter, invert=True)
        if got != want:
            failures.append(f"mode cursor {letter}")
            print(f"  mode cursor {letter}: MISMATCH")
        else:
            print(f"  mode cursor shows an inverted {letter}")
    m.poke(m.sym("KbdModState"), 0)
    m.call("NoteCurDraw")

    # The one that matters most. A drag blits the grabbed buffer, so
    # text typed since the last grab would vanish the moment the window
    # moved. NoteSync is what stops that, and it is deferred, so it is
    # exactly the kind of thing that works until it does not.
    m.poke(m.sym("NoteDirty"), 1)
    m.call("NoteSync")
    m.poke(m.sym("WinY"), winy + 16)
    m.call("BlitRect")
    moved = row0 + 16
    bad = [i for i, ch in enumerate("HELLO")
           if m.glyph_at(col0 + i, moved) != m.font_glyph(ch)]
    if bad:
        failures.append("text survives a drag")
        print(f"  after a 16 row move the text is wrong at columns {bad}")
    else:
        print("  text typed after the last grab survives a 16 row move")
    m.poke(m.sym("WinY"), winy)
    failures += pointer_key_checks(m)
    return failures


def blit_checks(m):
    """The bench's paint fingerprints, which A2 had to leave alone.

    Two equalities. WinDraw composing the window and BlitRect copying
    the grabbed buffer must land on identical pixels, which is what
    proved the WinDraw phase split and the blit faithful. And a six
    move drag erased the whole way must land on the same pixels as
    one erased only where damage says, which is what proved the
    damage rectangles.

    Both run through DevFillRect and DevFillDesk, so they are the
    check that A2 changed the interrupt behaviour of the fills and
    not what they paint. They live in the bench because that is
    where BScrSum and BMoveSeq are; they need no timing, so they run
    here rather than in Fuse.
    """
    def total():
        m.call("BScrSum")
        return m.m.hl

    failures = []
    m.call("BuildScrTab")       # AddrAt reads ScrTab, so nothing paints without it
    m.call("NoteNew")           # and WinDraw paints the document
    m.call("BClear")
    m.call("WinDraw")
    m.call("WinGrab")
    composed = total()
    m.call("BClear")
    m.call("BlitRect")
    copied = total()
    print("paint fingerprints:")
    print(f"  WinDraw {composed}, BlitRect {copied}")
    if composed != copied:
        failures.append("WinDraw and BlitRect disagree")
        print("  the composed window and the copied one are different pixels")

    sums = []
    for erase in ("WinErase", "WinEraseDamage"):
        m.call("BClear")
        m.poke16(m.sym("BEraseVec"), m.sym(erase))
        m.call("BMoveSeq")
        sums.append(total())
    print(f"  full erase {sums[0]}, damage erase {sums[1]}")
    if sums[0] != sums[1]:
        failures.append("the damage erase paints something else")
        print("  erasing only the damage does not land on the same pixels")
    return failures


def field_checks(m):
    """D2's PT_FIELD, driven the way a person would drive it.

    The settings panel has no field, so this table lives in the bench:
    a label, a name box ten characters wide on its own row, and an OK
    button. It is the shape G3 needs. Without it the field would be
    code that had never been run.
    """
    failures = []
    m.call("BuildScrTab")
    m.call("BClear")
    m.call("BPnlSetup")
    m.call("PnlPaint")
    buf, cap = m.sym("BPnlBuf"), m.sym("BPNLNAME")

    def text():
        out = ""
        for i in range(cap + 1):
            ch = m.peek(buf + i)
            if ch == 0:
                break
            out += chr(ch)
        return out

    def typed(keys):
        for k in keys:
            m.m.a = k if isinstance(k, int) else ord(k)
            m.call("PnlKey")

    print("D2 field:")
    typed("TAPE")
    print(f"  typed TAPE, buffer is {text()!r}, cursor at "
          f"{m.peek(m.sym('PnlFCur'))}")
    if text() != "TAPE" or m.peek(m.sym("PnlFCur")) != 4:
        failures.append("the field does not take typing")

    typed([1, 1, 1])                        # KEY_LEFT x3
    typed("X")
    if text() != "TXAPE":
        failures.append("the field does not insert at the cursor")
        print(f"  three lefts and an X gave {text()!r}, expected 'TXAPE'")
    else:
        print(f"  three lefts and an X insert rather than append: {text()!r}")

    typed([8])                              # KEY_DELETE
    if text() != "TAPE":
        failures.append("backspace does not close the gap")
        print(f"  backspace gave {text()!r}, expected 'TAPE'")
    else:
        print(f"  backspace closes the gap again: {text()!r}")

    typed([1] * 20)                         # run left off the front
    typed([8])                              # and backspace at column nought
    if text() != "TAPE" or m.peek(m.sym("PnlFCur")) != 0:
        failures.append("the field runs off its left edge")
        print(f"  left edge: {text()!r} cursor {m.peek(m.sym('PnlFCur'))}")
    else:
        print("  the cursor stops at column nought and backspace there does "
              "nothing")

    typed([2] * 20)                         # run right off the end
    typed("ABCDEFGHIJKLMNOPQR")             # and overfill it
    if len(text()) != cap:
        failures.append("the field overruns its capacity")
        print(f"  overfilling gave {len(text())} characters, capacity is {cap}")
    else:
        print(f"  it fills to {cap} characters and drops the rest: {text()!r}")

    # the cursor must be on screen, in the field, and only when focused
    col = m.sym("DLGX") + m.sym("DLGW") - 2 - cap        # one wider than it holds
    row = m.sym("DLGROW0") + 8
    cur = m.peek(m.sym("PnlFCur"))
    got = m.glyph_at(col + cur, row)
    if not all(b == 0xFF for b in got):
        failures.append("the field cursor is not on screen")
        print(f"  the cell at the cursor reads {got.hex()}, not an inverted one")
    else:
        print("  the cursor is an inverted cell, in the field, at the cursor")

    # an action row in the same panel still works
    m.m.a = 3                                # KEY_UP, off the field
    m.call("PnlKey")
    m.m.a = 4
    m.call("PnlKey")
    m.m.a = 4
    m.call("PnlKey")                         # down to OK
    m.m.a = 13                               # KEY_ENTER
    m.call("PnlKey")
    if m.peek(m.sym("BPnlHit")) != 1:
        failures.append("ENTER on an action row in a field panel does nothing")
    else:
        print("  ENTER on the OK row runs its routine")
    return failures


def heap_checks():
    """F1. The heap, and the window buffers coming out of it.

    Everything in this system was static. Two windows existed because
    two buffers were declared at fixed addresses, each 24 by 96
    whatever the window actually was, so the about window at 13 by 52
    cost 2,304 bytes to hold 676.

    The owner byte is the part worth having and it is checked
    directly: closing a window frees what it took without the window
    code remembering anything about it.
    """
    failures = []
    m = Machine("build/zxdesk.bin", "build/zxdesk.sym")
    for r in ("DetectMachine", "SetupIM2", "BuildScrTab", "StInit"):
        m.call(r)
    base, end = m.sym("HeapBase"), m.sym("HEAPEND")

    def walk():
        out, p = [], base
        while p < end:
            size = m.peek16(p)
            out.append((p, size, m.peek(p + 2), m.peek(p + 3)))
            if size == 0 and not m.peek(p + 2):
                break
            p += 4 + size
        return out

    def stat():
        m.call("HeapStat")
        return m.m.hl, m.m.de

    print("F1 heap:")
    m.call("HeapInit")
    if len(walk()) != 1 or walk()[0][2] != 0:
        failures.append("a fresh heap is not one free block")
    ptrs = []
    for size, owner in ((1152, 0x10), (676, 0x11), (300, 0x12)):
        m.m.bc = size
        m.m.a = owner
        m.call("HeapAlloc")
        if m.m.f & 1:
            failures.append(f"could not allocate {size}")
        ptrs.append(m.m.hl)
    blocks = walk()
    if [b[1] for b in blocks[:3]] != [1152, 676, 300]:
        failures.append("allocation does not split to the size asked for")
        print(f"  blocks came out as {[b[1] for b in blocks]}")
    else:
        print("  three allocations split off exactly what was asked for")

    # The payload is written over first, and that is the whole point of
    # it. HeapFree cleared the used flag and then put a nought in the
    # high byte of the size, and the damage healed itself: coalescing
    # read the zeros of an untouched payload as empty free blocks and
    # absorbed them four bytes at a time until it arrived back at the
    # right size. Against a block that had held window pixels it did
    # not heal, and the heap came back 2,619 bytes out of 7,112.
    m.m.set_memory_block(ptrs[1], bytes([0xA5]) * 676)
    free_before = stat()[0]
    m.m.hl = ptrs[1]
    m.call("HeapFree")
    if m.peek16(ptrs[1] - 4) != 676 or m.peek(ptrs[1] - 2) != 0:
        failures.append("freeing damages the block header")
        print(f"  header after the free: {m.peek16(ptrs[1] - 4)}, "
              f"used {m.peek(ptrs[1] - 2)}, owner {m.peek(ptrs[1] - 1)}")
    if stat()[0] != free_before + 676:
        failures.append("freeing does not return the block")
    # freeing the neighbour must merge all three free runs into one
    m.m.a = 0x12
    m.call("HeapFreeOwner")
    blocks = walk()
    if len(blocks) != 2 or blocks[1][2] != 0:
        failures.append("adjacent free blocks are not coalesced")
        print(f"  after freeing the neighbour: {blocks}")
    else:
        print(f"  freeing an adjacent block coalesces: "
              f"{len(blocks)} blocks, {stat()[0]} bytes free in one piece")
    if stat()[0] != stat()[1]:
        failures.append("the free space is not in one block after coalescing")

    # and the window system, which is what F1 was for
    m = boot(text="FOUR")
    fresh = stat()[0]
    opened = 0
    for _ in range(m.sym("WNDMAX") + 2):
        m.m.a = m.sym("APP_ABOUT")
        m.call("WndOpen")
        if not (m.m.f & 1):
            opened += 1
    if m.peek(m.sym("WndCount")) != m.sym("WNDMAX"):
        failures.append("the window table does not hold what it says")
        print(f"  {m.peek(m.sym('WndCount'))} windows, WNDMAX is "
              f"{m.sym('WNDMAX')}")
    else:
        print(f"  {opened} more windows opened, filling the table to "
              f"{m.sym('WNDMAX')}, each with a buffer sized to itself")

    sz, tab = m.sym("WNDRECSZ"), m.sym("WndTab")
    bufs = [m.peek(tab + i * sz + 6) | (m.peek(tab + i * sz + 7) << 8)
            for i in range(m.peek(m.sym("WndCount")))]
    if len(set(bufs)) != len(bufs) or 0 in bufs:
        failures.append("windows share a buffer, or one has none")
        print(f"  buffers: {[hex(b) for b in bufs]}")

    # Closing the front when the front is not the highest slot. The slot
    # for a new window used to be WndCount, which is only the free one
    # while closes come off the top, and they do not: WndClose closes
    # whatever was last pressed.
    while m.peek(m.sym("WndCount")) > 3:
        m.call("WndClose")
    m.m.a = 1
    m.call("WndFocusTo")
    m.call("WndClose")                  # closes slot 1, leaving 0 and 2
    m.m.a = m.sym("APP_ABOUT")
    m.call("WndOpen")
    z = [m.peek(m.sym("WndZ") + i)
         for i in range(m.peek(m.sym("WndCount")))]
    bufs = [m.peek16(tab + slot * sz + 6) for slot in z]
    if len(set(z)) != len(z) or len(set(bufs)) != len(bufs) or 0 in bufs:
        failures.append("a reopened window reuses a live slot")
        print(f"  z order {z}, buffers {[hex(b) for b in bufs]}")
    else:
        print(f"  closing the middle of three and opening again takes the "
              f"slot that was freed, not the count: z order {z}")

    # And a table slot with no pixels behind it. WndOpen checks the
    # count before it asks the heap, so the two can disagree, and the
    # window that is half opened when they do is not in the z order.
    while m.peek(m.sym("WndCount")) > 1:
        m.call("WndClose")
    m.m.bc = stat()[1]                  # take the whole heap
    m.m.a = 0
    m.call("HeapAlloc")
    hog = m.m.hl
    front, rec = m.peek(m.sym("WndZ")), m.peek16(m.sym("WinBufP"))
    m.m.a = m.sym("APP_ABOUT")
    m.call("WndOpen")
    if not (m.m.f & 1):
        failures.append("a window opened with no heap left")
    elif m.peek(m.sym("WndCur")) != front or m.peek16(m.sym("WinBufP")) != rec:
        failures.append("a refused open leaves the wrong window live")
        print(f"  WndCur {m.peek(m.sym('WndCur'))}, front {front}, "
              f"buffer {hex(m.peek16(m.sym('WinBufP')))}, was {hex(rec)}")
    else:
        print("  with the heap full the open is refused and the window that "
              "was live still is")
    m.m.hl = hog
    m.call("HeapFree")

    if stat()[0] != fresh:
        failures.append("closing windows does not give the heap back")
        print(f"  heap is {stat()[0]} free, was {fresh} before they opened")
    else:
        print(f"  closing them all returns every byte, by owner, without the "
              f"window code remembering what it took")
    return failures


def app_checks():
    """F2. Two notepads, and two documents rather than two views of one.

    This is the whole package in one check. The notepad was a set of
    globals, so a second one would have been a second set; it is a
    block from the heap under the window's own index now, and the live
    copy is swapped the way B4 swaps the window record and D3 swaps
    the panel.

    Typing goes through HdlKey rather than NoteKey, because the point
    is not that the editor works. It is that the key reaches whatever
    owns the front window, through a descriptor, without HdlKey
    knowing that notepads exist.
    """
    failures = []
    m = boot(text="")

    def stat():
        m.call("HeapStat")
        return m.m.hl

    def typed(text):
        for ch in text:
            m.m.b = ord(ch)
            m.call("HdlKey")

    def row0():
        base = m.sym("NoteBuf")
        return "".join(chr(m.peek(base + c))
                       for c in range(m.sym("NOTECOLS"))).rstrip()

    def rec(slot, off):
        return m.peek(m.sym("WndTab") + slot * m.sym("WNDRECSZ") + off)

    def recw(slot, off):
        return rec(slot, off) | (rec(slot, off + 1) << 8)

    print("F2 application model:")
    fresh = stat()
    typed("ONE")
    m.m.a = m.sym("APP_NOTE")
    m.call("WndOpen")
    if m.m.f & 1:
        failures.append("a second notepad would not open")
        return failures
    typed("TWO")

    if row0() != "TWO":
        failures.append("the second notepad is not the live document")
        print(f"  the live document reads {row0()!r}")

    # the two instances, and what makes them two
    bufs = [recw(s, 6) for s in (0, 1)]
    states = [recw(s, 10) for s in (0, 1)]
    if len(set(bufs)) != 2 or len(set(states)) != 2 or 0 in states:
        failures.append("two notepads share a buffer or a document")
        print(f"  buffers {[hex(b) for b in bufs]}, "
              f"documents {[hex(s) for s in states]}")
    else:
        print(f"  two notepads, two buffers and two documents, all from "
              f"the heap: {[hex(s) for s in states]}")

    # a cascade, so two of the same application do not look like one
    if (rec(0, 0), rec(0, 1)) == (rec(1, 0), rec(1, 1)):
        failures.append("the second instance opens exactly on the first")
    else:
        print(f"  and at {rec(1, 0)},{rec(1, 1)} rather than on top of "
              f"{rec(0, 0)},{rec(0, 1)}")

    # the swap, which is the package
    m.m.a = 0
    m.call("WndFocusTo")
    first = row0()
    m.m.a = 1
    m.call("WndFocusTo")
    second = row0()
    if (first, second) != ("ONE", "TWO"):
        failures.append("switching windows does not switch documents")
        print(f"  window nought reads {first!r}, window one reads {second!r}")
    else:
        print("  raising one and then the other brings its own text back: "
              "'ONE' then 'TWO'")

    # and closing gives the document back without the notepad being asked
    # about the memory. It is asked about the work, so this says it means
    # it: the close vector arrived after F2 and close_checks is where it
    # is tested, not here.
    m.poke(m.sym("WndForce"), 1)
    m.call("WndClose")
    if row0() != "ONE":
        failures.append("closing the front window leaves the wrong document")
        print(f"  the live document reads {row0()!r}")
    if stat() != fresh:
        failures.append("closing a notepad does not return its document")
        print(f"  heap is {stat()} free, was {fresh} before it opened")
    else:
        print("  closing it returns the pixels and the document together, "
              "by owner, with no teardown vector to forget")

    # the about window keeps nothing, so the swap must not move anything
    m.m.a = m.sym("APP_ABOUT")
    m.call("WndOpen")
    typed("XX")
    m.m.a = 0
    m.call("WndFocusTo")
    if row0() != "ONE":
        failures.append("an application with no state disturbs one that has")
        print(f"  after the about window: {row0()!r}")
    else:
        print("  an application that keeps nothing costs the swap a compare")

    # FILE, NEW opens one rather than blanking the one there is, and the
    # items that act on a document refuse when there is not one in front
    was = m.peek(m.sym("WndCount"))
    m.call("NoteMenuNew")
    if m.peek(m.sym("WndCount")) != was + 1 or row0() != "":
        failures.append("NEW does not open a notepad")
        print(f"  {m.peek(m.sym('WndCount'))} windows, live document "
              f"{row0()!r}")
    else:
        print(f"  NEW opens another notepad rather than blanking this one: "
              f"{was} windows to {m.peek(m.sym('WndCount'))}")
    z = [m.peek(m.sym("WndZ") + i)
         for i in range(m.peek(m.sym("WndCount")))]
    about = [slot for slot in z if recw(slot, 8) == m.sym("AppAbout")]
    if not about:
        failures.append("the about window is not in the table to test with")
        return failures
    m.m.a = about[0]
    m.call("WndFocusTo")
    m.call("FmSavePanel")
    if m.peek(m.sym("FmUp")):
        failures.append("SAVE AS opens over a window with no document")
        m.poke(m.sym("FmUp"), 0)
    else:
        print("  and SAVE AS refuses when the window in front has no "
              "document to save")

    # ABOUT raises the one that is already open, and it used to find it
    # by assuming slot one, which F2 makes a notepad
    m.call("AboutOpen")
    zz = [m.peek(m.sym("WndZ") + i)
          for i in range(m.peek(m.sym("WndCount")))]
    if recw(zz[0], 8) != m.sym("AppAbout") or len(zz) != len(z):
        failures.append("ABOUT does not find the about window that is open")
        print(f"  z {zz}, front runs {hex(recw(zz[0], 8))}")
    else:
        print("  ABOUT raises the about window wherever its slot is, "
              "rather than assuming slot one")
    return failures


def app_partial_checks():
    """F2. A window that gets its pixels and then cannot get a document.

    WndOpen asks the heap twice now, so the second ask can fail after
    the first has succeeded. That window never reaches the z order and
    so never reaches WndClose, and what it took would never come back.
    The heap is squeezed to exactly the buffer's size to arrange it.
    """
    failures = []
    m = boot(text="")
    print("F2 half opened windows:")

    def stat():
        m.call("HeapStat")
        return m.m.hl, m.m.de

    def row0():
        base = m.sym("NoteBuf")
        return "".join(chr(m.peek(base + c))
                       for c in range(m.sym("NOTECOLS"))).rstrip()

    for ch in "KEEP":
        m.m.b = ord(ch)
        m.call("HdlKey")
    fresh = stat()[0]

    # leave a hole that fits the 16 by 72 buffer and nothing after it
    want = 16 * 72
    m.m.bc = stat()[1] - want - m.sym("HEAPHDR")
    m.m.a = 0
    m.call("HeapAlloc")
    hog = m.m.hl
    m.m.a = m.sym("APP_NOTE")
    m.call("WndOpen")
    if not (m.m.f & 1):
        failures.append("a notepad opened with no room for its document")
    m.m.hl = hog
    m.call("HeapFree")
    if stat()[0] != fresh:
        failures.append("a half opened window keeps what it took")
        print(f"  heap is {stat()[0]} free, was {fresh}")
    elif row0() != "KEEP":
        failures.append("a refused open loses the live document")
        print(f"  the live document reads {row0()!r}")
    else:
        print("  one that gets pixels and then no document gives the "
              "pixels back, and the window in front keeps its text")
    return failures


def sound_checks():
    """Sound, which the desktop has not had at all until now.

    There is no way to hear a beep from here, so what is checked is
    the only evidence a beep leaves: what went out of which port. That
    turns out to be the whole of it, because the two machines make
    sound in completely different ways and the interesting claim is
    that each one is asked properly.

    A 48K has a beeper on one bit of the ULA port and a tone is the
    processor toggling it in a counted loop. A 128K has an AY with an
    envelope generator, so the chip plays the note on its own and the
    processor writes seven registers and leaves.
    """
    failures = []
    print("sound:")

    def watched(banked):
        m = Machine("build/zxdesk.bin", "build/zxdesk.sym", banked=banked)
        writes = []
        inner = m._out

        def spy(addr, value):
            writes.append((addr & 0xFFFF, value))
            inner(addr, value)

        m.m.set_output_callback(spy)
        for r in ("DetectMachine", "SetupIM2", "BuildScrTab", "MouseInit",
                  "JoyInit", "StInit", "SetLoad", "SetApply", "DskInit",
                  "InitScreen", "WndInit"):
            m.call(r)
        writes.clear()
        return m, writes

    m, w = watched(False)
    m.m.a = m.sym("SND_BEEP")
    m.call("SndPlay")
    ula = [v for a, v in w if (a & 0xFF) == 0xFE]
    toggles = sum(1 for i in range(1, len(ula)) if (ula[i] ^ ula[i - 1]) & 0x10)
    borders = sorted(set(v & 7 for v in ula))
    if toggles < 100:
        failures.append("a 48K beep does not drive the speaker")
        print(f"  {toggles} toggles")
    elif borders != [7]:
        failures.append("the beeper changes the border colour")
        print(f"  border bits seen: {borders}")
    else:
        print(f"  a 48K beep toggles the speaker {toggles} times and leaves "
              f"the border white, because the border goes out of the same "
              f"port and is carried through every toggle")

    m, w = watched(True)
    m.m.a = m.sym("SND_BEEP")
    m.call("SndPlay")
    regs = [(s, d) for (a1, s), (a2, d) in zip(w[::2], w[1::2])
            if a1 == 0xFFFD and a2 == 0xBFFD]
    beeper = sum(1 for a, v in w if (a & 0xFF) == 0xFE)
    want = [(0, 111), (1, 0), (7, 0x3E), (8, 0x10), (11, 24), (12, 0), (13, 0)]
    if regs != want:
        failures.append("the 128K beep is not the AY registers expected")
        print(f"  {regs}")
    elif beeper:
        failures.append("a 128K also drives the beeper")
    else:
        print("  a 128K writes seven AY registers instead: tone, mixer, "
              "volume from the envelope, envelope period, and the shape "
              "last because writing it is what starts the note")
        print("  shape nought decays once and stays down, so nothing has to "
              "come back and turn it off")

    m, w = watched(False)
    m.poke(m.sym("SetSound"), 0)
    m.m.a = m.sym("SND_BEEP")
    m.call("SndPlay")
    if w:
        failures.append("sound plays with the setting off")
        print(f"  {len(w)} port writes while silenced")
    else:
        print("  and with SOUND off not one port is written, because a "
              "machine that beeps and cannot be told to stop is worse than "
              "one that never beeped")

    # the hooks, which are where it is actually heard
    m, w = watched(False)
    m.m.a = 1
    m.call("MenuAction")                # ZX DESK, SETTINGS
    if not any((a & 0xFF) == 0xFE for a, v in w):
        failures.append("picking a menu item is silent")
    else:
        print("  picking a menu item clicks, hooked once in MenuAction "
              "rather than once per item")
    m, w = watched(False)
    m.m.hl = m.sym("TxtWinTitle")
    m.m.de = 0
    m.call("DlgAlert")
    if not any((a & 0xFF) == 0xFE for a, v in w):
        failures.append("an alert is silent")
    else:
        print("  and an alert beeps, which is the one that earns the "
              "feature: an alert you miss has not been delivered")
    return failures


def scrollbar_checks():
    """The control D2 specified and never built.

    A window that hides part of its contents and does not say so is
    lying quietly, and the notepad has been scrolling sixteen lines
    through seven since this morning without a word about it.

    The interface is two vectors and no drawing: the application says
    how many units there are, how many are showing and which is
    first, and it is told which to make first. The frame has no
    business knowing that a unit is a line of text in one window and
    a filename in another.
    """
    failures = []
    print("scroll bars:")

    def filled(n):
        m = boot(text="")
        for i in range(n):
            for ch in f"LINE {i}":
                m.m.b = ord(ch)
                m.call("HdlKey")
            m.m.b = m.sym("KEY_ENTER")
            m.call("HdlKey")
        return m

    def info(m):
        m.call("WndScrollInfo")
        if m.m.f & 1:
            return None
        return m.m.a, m.m.bc >> 8, m.m.bc & 0xFF

    m = filled(12)
    got = info(m)
    if got != (6, 13, 7):
        failures.append("the notepad's scroll model is wrong")
        print(f"  first, total, showing = {got}")
    else:
        print(f"  twelve lines in a seven line window: first {got[0]}, "
              f"{got[1]} reachable, {got[2]} showing")

    # the bar on screen, against the numbers behind it
    x, y, w, h = (m.peek(m.sym(n)) for n in ("WinX", "WinY", "WinW", "WinH"))
    col = x + w - 2
    m.m.a = 3
    m.call("NoteScrollTo")
    bar = [m.peek(m.scr_addr(col, r))
           for r in range(y + m.sym("WinCapH"), y + h - 1)]
    thumb = [i for i, b in enumerate(bar) if b == 0xFF]
    arrow = m.sym("SCRARROW")
    body = thumb[1:-1] if len(thumb) > 2 else thumb   # the arrows' bases
    ty, th = m.peek(m.sym("ScrThumbY")), m.peek(m.sym("ScrThumbH"))
    want = list(range(arrow + ty, arrow + ty + th))
    if body != want:
        failures.append("the thumb is not where the numbers say")
        print(f"  thumb rows {body}, expected {want}")
    else:
        print(f"  the thumb sits at row {ty} of the trough and is {th} long, "
              f"which is where and how big the model says")

    m.m.bc = (col << 8) | (y + 20)
    m.call("WndHitTest")
    onbar = m.m.a
    m.m.bc = ((x + 1) << 8) | (y + 20)
    m.call("WndHitTest")
    ontext = m.m.a
    if onbar != m.sym("CTL_SCROLL") or ontext != m.sym("CTL_WININTERIOR"):
        failures.append("the bar is not hit tested apart from the interior")
        print(f"  on the bar {onbar}, on the text {ontext}")
    else:
        print("  a press on the bar is not a press on the text beside it")

    def press(m, py):
        m.poke(m.sym("PtrX"), (x + w - 2) * 8 + 4)
        m.poke(m.sym("PtrY"), py)
        m.poke(m.sym("Buttons"), 0xFD)
        m.call("EvPoll")
        m.call("EvDispatch")
        m.poke(m.sym("Buttons"), 0xFF)
        m.call("EvPoll")
        m.call("EvDispatch")
        return m.peek(m.sym("NoteTop"))

    top0 = m.peek(m.sym("NoteTop"))
    down = press(m, y + h - 4)
    up = press(m, y + m.sym("WinCapH") + 2)
    if down != top0 + 1 or up != top0:
        failures.append("the arrows do not move the view by one")
        print(f"  from {top0}: down {down}, up {up}")
    else:
        print(f"  the arrows move the view a line: {top0} down to {down} and "
              f"back up to {up}")

    for _ in range(4):
        press(m, y + m.sym("WinCapH") + 2)
    if m.peek(m.sym("NoteTop")) != 0:
        failures.append("the view does not reach the top")
    page = press(m, y + h - 12)
    if page != 6:
        failures.append("the channel does not move a page")
        print(f"  a page from the top landed on {page}")
    else:
        print(f"  the channel moves a page, from the top to {page}, and the "
              f"ends hold: it stops at nought and at the last reachable row")

    # a bar takes a column of the interior, and the text has to know
    m = boot(text="")
    for ch in "MMMMMMMMMMMMMM":
        m.m.b = ord(ch)
        m.call("HdlKey")
    for i in range(20):
        m.m.b = m.sym("KEY_ENTER")
        m.call("HdlKey")
    m.call("WndRepaintAll")
    col = m.peek(m.sym("WinX")) + m.peek(m.sym("WinW")) - 2
    row = m.peek(m.sym("WinY")) + m.sym("WinCapH") + 20
    b = m.peek(m.scr_addr(col, row))
    if b not in (0x81, 0xFF):
        failures.append("text is drawn over the scroll bar's column")
        print(f"  the bar column reads {b:02X}")
    else:
        print("  a line of fourteen characters stops before the bar rather "
              "than being drawn through it")

    # and a window with nothing to scroll has no bar at all
    m = boot(text="")
    m.m.a = m.sym("APP_CLOCK")
    m.call("WndOpen")
    if info(m) is not None:
        failures.append("the clock has a scroll bar")
    else:
        print("  the clock has no bar, because a clock has nothing that is "
              "off the bottom")
    return failures


def printer_checks():
    """Printing, which is a storage backend rather than a special case.

    A printer is a write only device with no directory, and that is
    exactly what the capability byte was invented to describe. So it
    goes in the registry beside RAM, tape and the banks, and two
    things fall out for nothing: the commander gets a third pane, and
    copying a file onto it prints the file.

    Whether ink reaches paper is a Fuse question and Fuse answers it,
    because it emulates a ZX Printer and writes what comes out to a
    file. What is checkable here is everything up to the wire: that
    the registry entry is right, that the device refuses what it
    cannot do rather than attempting it, and that what the notepad
    hands over is lines and newlines rather than a memory dump.

    That last one is checked by swapping the RAM backend in behind
    the printer's registry row and printing at it. If the notepad can
    tell the difference, the printer was not really a device.
    """
    failures = []
    print("printing:")
    m = boot(text="")

    m.m.a = m.sym("ST_PRINT")
    m.call("StSelect")
    if m.m.f & 1 or m.peek(m.sym("StBackend")) != m.sym("ST_PRINT"):
        failures.append("the printer is not in the registry")
        return failures
    m.call("StCaps")
    caps = m.m.a
    want = m.sym("STCAP_WRITE") | m.sym("STCAP_PERSIST")
    if caps != want:
        failures.append("the printer's capabilities are wrong")
        print(f"  {caps:02X}, expected {want:02X}")
    else:
        print("  it registers as write and persist, with no directory and "
              "no random access, which is what a roll of paper is")

    refused = []
    m.m.a = 0
    m.call("StDir")
    refused.append(bool(m.m.f & 1))
    m.m.hl = m.sym("NoteName")
    m.m.bc = 4
    m.call("StRead")
    refused.append(bool(m.m.f & 1))
    m.m.hl = m.sym("NoteName")
    m.m.b = m.sym("FA_READ")
    m.call("StOpen")
    refused.append(bool(m.m.f & 1))
    if not all(refused):
        failures.append("the printer allows something it cannot do")
        print(f"  dir, read, open-to-read refused: {refused}")
    else:
        print("  listing it, reading it and opening it to read are all "
              "refused rather than attempted")

    # no printer on the bus, so nothing may call the ROM
    m.m.hl = m.sym("NoteName")
    m.m.b = m.sym("FA_OVERWRITE")
    m.call("StOpen")
    if not (m.m.f & 1) or m.m.a != m.sym("STERR_NOTFOUND"):
        failures.append("opening a printer that is not there is allowed")
    else:
        print("  with nothing on the bus the open is refused before the ROM "
              "is called, because the ROM's printer routines are written "
              "for a machine that has one")

    # and what the notepad actually hands over. The RAM backend goes in
    # behind the printer's registry row, so NotePrint prints at RAM.
    m = boot(text="HELLO")
    m.m.b = m.sym("KEY_ENTER")
    m.call("HdlKey")
    for ch in "WORLD":
        m.m.b = ord(ch)
        m.call("HdlKey")
    # Find the printer's row rather than counting to it. This was
    # 3 * STREGSZ, and adding the esxDOS backend in front of the
    # printer moved the printer to row four, so the poke landed on
    # esxDOS and NotePrint went to a printer that is not on the bus.
    # The failure read "nothing was printed", which was true and
    # said nothing about the cause.
    row = m.sym("StRegistry")
    while m.peek(row) != m.sym("ST_PRINT"):
        row += m.sym("STREGSZ")
    for off, name in ((2, "RamOpen"), (4, "RamClose"), (6, "RamRead"),
                      (8, "RamWrite"), (10, "RamDirEnt"), (12, "RamDelete")):
        m.poke16(row + off, m.sym(name))
    m.call("NotePrint")
    m.m.a = m.sym("ST_RAM")
    m.call("StSelect")
    m.m.hl = m.sym("NoteName")
    m.m.b = m.sym("FA_READ")
    m.call("StOpen")
    if m.m.f & 1:
        failures.append("nothing was printed")
        return failures
    handle = m.m.a
    m.m.hl = m.sym("ClipBuf")
    m.m.bc = 40
    m.call("StRead")
    got = bytes(m.peek(m.sym("ClipBuf") + i) for i in range(m.m.bc))
    m.m.a = handle
    m.call("StClose")
    if got != b"HELLO\rWORLD":
        failures.append("the printed stream is not the document's lines")
        print(f"  {got!r}")
    else:
        print(f"  the notepad sends {got!r}: the lines and one ENTER "
              f"between them, not the 256 byte block a save writes")
        print("  and it could not tell it was printing at RAM, which is "
              "what makes the printer a device rather than a special case")
    return failures


def shortcut_checks():
    """Keyboard shortcuts that cannot be confused with typing.

    A Spectrum has no control key and no alt key. It has two shifts,
    and holding both is EXTEND MODE, which is where the machine's own
    ROM puts a third meaning for a key. That is the modifier because
    it is the only one that exists.

    Not interfering with typing is structural rather than careful: a
    shortcut is not a letter with a flag on it, it is a code above
    everything the three text tables can produce, and HdlKey takes it
    before a window is selected. So the check that matters is the
    sweep: every key, under every modifier, and nothing but EXTEND
    ever yields one.
    """
    failures = []
    print("keyboard shortcuts:")
    m = boot(text="")

    def decode(index, mods):
        m.poke(m.sym("KbdModState"), mods)
        m.m.a = index
        m.call("KbdDecode")
        return m.m.a

    leaked = []
    for index in range(40):
        for mods in (0, 1, 2):              # none, CAPS, SYMBOL
            if decode(index, mods) >= m.sym("SC_BASE"):
                leaked.append((index, mods))
    extend = [index for index in range(40)
              if decode(index, 3) >= m.sym("SC_BASE")]
    if leaked:
        failures.append("a shortcut code can be typed without both shifts")
        print(f"  {leaked}")
    elif len(extend) != m.sym("SCCOUNT"):
        failures.append("the extend table does not hold every shortcut")
        print(f"  {len(extend)} of {m.sym('SCCOUNT')}")
    else:
        print(f"  every key under no shift, CAPS and SYMBOL yields text and "
              f"never a shortcut; both shifts yields {len(extend)} of them "
              f"and no text")

    # the pointer keys are read from the ports, so under extend they must
    # decode to nothing at all or they would do two things at once
    pointer = {10: "Q", 5: "A", 26: "O", 25: "P", 19: "5", 24: "6", 23: "7",
               22: "8"}
    both = [k for k in pointer if decode(k, 3) != 0]
    if both:
        failures.append("a pointer key also posts a shortcut")
        print(f"  {[pointer[k] for k in both]}")
    else:
        print("  the pointer keys are nought under both shifts, so they move "
              "the pointer and do nothing else, which they did not before")

    # typing is untouched
    m = boot(text="")
    for ch in "HELLO":
        m.m.b = ord(ch)
        m.call("HdlKey")
    base, cols = m.sym("NoteBuf"), m.sym("NOTECOLS")
    got = "".join(chr(m.peek(base + c)) for c in range(cols)).rstrip()
    if got != "HELLO":
        failures.append("ordinary keys no longer reach the notepad")
        print(f"  the document reads {got!r}")
    else:
        print(f"  and ordinary keys still reach the document: {got!r}")

    def shortcut(m, name):
        m.m.b = m.sym(name)
        m.call("HdlKey")

    # each one does its own thing
    m = boot(text="")
    n = m.peek(m.sym("WndCount"))
    for name, want in (("SC_NEW", 1), ("SC_CLOCK", 1), ("SC_CALENDAR", 1)):
        shortcut(m, name)
        if m.peek(m.sym("WndCount")) != n + want:
            failures.append(f"{name} did not open a window")
        n = m.peek(m.sym("WndCount"))
    shortcut(m, "SC_CLOCK")                 # already open, so raised
    if m.peek(m.sym("WndCount")) != n:
        failures.append("SC_CLOCK opened a second clock")
    else:
        print(f"  N, K and L open a notepad, a clock and a calendar, and a "
              f"second K raises the clock rather than making another")

    front = m.peek(m.sym("WndZ"))
    shortcut(m, "SC_NEXT")
    if m.peek(m.sym("WndZ")) == front:
        failures.append("X does not bring the window behind forward")
    else:
        print("  X brings the back window forward, so pressing it walks the "
              "whole order rather than swapping two")

    g0 = [m.peek(m.sym("WndTab") + m.peek(m.sym("WndZ") + i) *
                 m.sym("WNDRECSZ")) for i in range(m.peek(m.sym("WndCount")))]
    shortcut(m, "SC_TILE")
    m.call("WndFlush")
    g1 = [m.peek(m.sym("WndTab") + m.peek(m.sym("WndZ") + i) *
                 m.sym("WNDRECSZ")) for i in range(m.peek(m.sym("WndCount")))]
    if g0 == g1:
        failures.append("T does not tile")
    else:
        print("  C and T reach cascade and tile without the menu")

    n = m.peek(m.sym("WndCount"))
    shortcut(m, "SC_CLOSE")
    if m.peek(m.sym("WndCount")) != n - 1:
        failures.append("W does not close the front window")
    else:
        print("  W closes the front window, and it goes through the same "
              "close vector, so a notepad with unsaved work still asks")

    # a dialogue swallows them, because a panel that can be walked out
    # from under by a shortcut is not modal
    m = boot(text="")
    m.m.a = m.sym("APP_NOTE")
    m.call("WndOpen")
    for ch in "KEEP":
        m.m.b = ord(ch)
        m.call("HdlKey")
    m.call("WndClose")                      # puts the unsaved question up
    n = m.peek(m.sym("WndCount"))
    shortcut(m, "SC_NEW")
    if m.peek(m.sym("WndCount")) != n:
        failures.append("a shortcut works while a dialogue is up")
    else:
        print("  and a dialogue swallows them, so a modal panel stays modal")
    return failures


def arrange_checks():
    """Cascade and tile, which is GEM's desktop tidying up after you.

    The two are different kinds of operation. Cascade moves windows
    and does not resize them, so it touches no memory and cannot fail.
    Tile resizes them, so every window gives its buffer back and asks
    for another, and the heap can refuse.

    Tiling is also the first thing that ever made a window narrower
    than the size its application asked for, and applications drew
    their content assuming the window was wide enough for it. A
    sixteen column calendar ran its grid straight through the window
    beside it.
    """
    failures = []
    print("arranging the windows:")
    SZ = m0 = None

    def opened(apps):
        m = boot(text="HELLO")
        m.call("ClkInit")
        for a in apps:
            m.m.a = m.sym(a)
            m.call("WndOpen")
        return m

    def geom(m):
        m.call("WndFlush")
        out = []
        for i in range(m.peek(m.sym("WndCount"))):
            slot = m.peek(m.sym("WndZ") + i)
            b = m.sym("WndTab") + slot * m.sym("WNDRECSZ")
            out.append(tuple(m.peek(b + k) for k in range(4)))
        return out

    m = opened(("APP_ABOUT", "APP_CLOCK", "APP_CAL"))
    m.call("WndCascade")
    g = geom(m)                             # front first
    steps = [(g[i][0] - g[i + 1][0], g[i][1] - g[i + 1][1])
             for i in range(len(g) - 1)]
    if any(s != (2, 10) for s in steps):
        failures.append("cascade does not step evenly")
        print(f"  {g}")
    elif any(x + w > 32 or y + h > m.sym("STATRULE") for x, y, w, h in g):
        failures.append("cascade puts a window off the screen")
        print(f"  {g}")
    else:
        print(f"  four windows step two columns and ten rows each, front "
              f"furthest, none off the edge: {g[-1][:2]} back to {g[0][:2]}")

    # tile, and every buffer sized to the tile it got
    for apps, want in (
            ((), [(0, 9, 32, 174)]),
            (("APP_CLOCK",), [(0, 9, 16, 174), (16, 9, 16, 174)]),
            (("APP_CLOCK", "APP_CAL", "APP_ABOUT"),
             [(0, 9, 16, 87), (0, 96, 16, 87),
              (16, 9, 16, 87), (16, 96, 16, 87)])):
        m = opened(apps)
        m.call("WndTile")
        g = sorted(geom(m))
        if g != sorted(want):
            failures.append(f"tiling {len(want)} windows is wrong")
            print(f"  {len(want)} windows: {g}")
            continue
        bad = []
        for i in range(m.peek(m.sym("WndCount"))):
            slot = m.peek(m.sym("WndZ") + i)
            b = m.sym("WndTab") + slot * m.sym("WNDRECSZ")
            w, h = m.peek(b + 2), m.peek(b + 3)
            buf = m.peek(b + 6) | (m.peek(b + 7) << 8)
            if buf == 0 or m.peek16(buf - 4) != w * h:
                bad.append((w, h, hex(buf)))
        if bad:
            failures.append(f"a tiled window's buffer is not its own size")
            print(f"  {bad}")
        else:
            print(f"  {len(want)} windows tile to {want[0][2]} by "
                  f"{want[0][3]}, each with a buffer of exactly its own size")

    # and nothing paints outside the window it belongs to
    m = boot(text="")
    m.m.a = m.sym("APP_CAL")
    m.call("WndOpen")                       # the z order is calendar, notepad
    m.poke(m.sym("WndCount"), 1)            # so hiding the notepad leaves the
                                            # calendar as the only window, and
                                            # the whole desktop to tile it into
    m.call("WndTile")                       # one window: the whole desktop
    m.poke(m.sym("WinW"), 16)               # then squeeze it to half
    m.call("WndAllocBuf")
    # The reference is the bare desktop on this same machine, because a
    # second boot would have a notepad sitting in the columns being
    # checked and the comparison would be about that instead.
    m.call("DrawDesktop")
    y0 = m.peek(m.sym("WinY"))
    rows = range(y0, min(y0 + m.peek(m.sym("WinH")), 182))
    ref = {(c, r): m.peek(m.scr_addr(c, r))
           for r in rows for c in range(16, 32)}
    m.call("WndRepaintAll")
    spill = sum(1 for (c, r), v in ref.items()
                if m.peek(m.scr_addr(c, r)) != v)
    if spill:
        failures.append("a calendar narrower than it likes paints outside "
                        "its window")
        print(f"  {spill} bytes of it landed to the right of the window")
    else:
        print("  a calendar squeezed to sixteen columns clips its grid at "
              "the border instead of running through whatever is beside it")
    return failures


def commander_checks():
    """The two pane file manager, over devices rather than directories.

    There are no directories on this machine and there never will be,
    so the panes are the storage layer's devices: RAM, tape, and on a
    128K the spare banks. A copy between two panes is a copy between
    two backends, which is the thing E1 was built for and had never
    been asked to do.

    A pane on a device with no directory says so, and that is
    STCAP_DIR being asked rather than an error path bolted on.
    """
    failures = []
    print("two pane file manager:")

    def named(m, name):
        for i, ch in enumerate(name):
            m.poke(m.sym("NoteName") + i, ord(ch))
        m.poke(m.sym("NoteName") + len(name), 0)
        m.call("NoteSave")

    def panes(m):
        font = {}
        for c in range(32, 128):
            font[m.font_glyph(chr(c))] = chr(c)
            font[m.font_glyph(chr(c), invert=True)] = chr(c)
        x, y = m.peek(m.sym("WinX")) + 1, m.peek(m.sym("WinY"))
        out = []
        for col in (x, x + m.sym("CMDPANEW") + 2):
            head = "".join(font.get(m.glyph_at(col + i, y + m.sym("CMDHEADY")),
                                    " ") for i in range(6)).strip()
            rows = []
            for r in range(6):
                t = "".join(font.get(
                    m.glyph_at(col + i, y + m.sym("CMDLISTY") + r * 8), " ")
                    for i in range(8)).strip()
                if t:
                    rows.append(t)
            out.append((head, rows))
        return out

    m = boot(text="HELLO")
    for name in ("NOTE", "DRAFT", "LETTER"):
        named(m, name)
    m.m.a = m.sym("APP_CMD")
    m.call("WndOpen")
    left, right = panes(m)
    if left != ("RAM", ["NOTE", "DRAFT", "LETTER"]):
        failures.append("the left pane does not list the RAM device")
        print(f"  left is {left}")
    elif right != ("TAPE", ["NO DIR"]):
        failures.append("the tape pane does not say it has no directory")
        print(f"  right is {right}")
    else:
        print(f"  left {left[0]} lists {left[1]}, right {right[0]} says "
              f"{right[1][0]}, which is STCAP_DIR answering rather than an "
              f"error path")

    # walking a pane, and changing which pane
    for _ in range(2):
        m.m.b = m.sym("KEY_DOWN")
        m.call("HdlKey")
    if m.peek(m.sym("CmdSel0")) != 2:
        failures.append("down does not walk the pane")
    m.m.b = m.sym("KEY_DOWN")
    m.call("HdlKey")                        # already on the last one
    if m.peek(m.sym("CmdSel0")) != 2:
        failures.append("down runs off the end of the listing")
    else:
        print("  down walks the listing and stops at the last file rather "
              "than counting past it")
    m.m.b = m.sym("KEY_RIGHT")
    m.call("HdlKey")
    if m.peek(m.sym("CmdActive")) != 1:
        failures.append("right does not move to the other pane")
    else:
        print("  right moves to the other pane and the header inverts to "
              "say so")
    m.m.b = m.sym("KEY_LEFT")
    m.call("HdlKey")

    # ENTER opens the selected file in a notepad of its own
    before = m.peek(m.sym("WndCount"))
    m.m.b = m.sym("KEY_ENTER")
    m.call("HdlKey")
    base, cols = m.sym("NoteBuf"), m.sym("NOTECOLS")
    got = "".join(chr(m.peek(base + c)) for c in range(cols)).rstrip()
    if m.peek(m.sym("WndCount")) != before + 1:
        failures.append("ENTER does not open the file in a window")
    elif got != "HELLO":
        failures.append("the file opened is not the one selected")
        print(f"  the new notepad reads {got!r}")
    else:
        print(f"  ENTER opens LETTER in a notepad of its own reading "
              f"{got!r}, rather than over whatever notepad was behind it")

    # D deletes from the live pane
    m = boot(text="HELLO")
    for name in ("NOTE", "DRAFT"):
        named(m, name)
    m.m.a = m.sym("APP_CMD")
    m.call("WndOpen")
    m.m.b = ord("D")
    m.call("HdlKey")
    left, _ = panes(m)
    if left[1] != ["DRAFT"]:
        failures.append("D does not delete the selected file")
        print(f"  the left pane is {left}")
    else:
        print("  D deletes the file the keys are on and the listing closes "
              "up behind it")

    # and a copy between two devices, which is the whole point
    m = Machine("build/zxdesk.bin", "build/zxdesk.sym", banked=True)
    for r in ("DetectMachine", "SetupIM2", "BuildScrTab", "MouseInit",
              "JoyInit", "StInit", "SetLoad", "SetApply", "DskInit",
              "InitScreen", "WndInit"):
        m.call(r)
    for ch in "COPY ME":
        m.call_with_a("NoteKey", ord(ch))
    named(m, "XFER")                        # onto the banks, which a 128K picks
    m.m.a = m.sym("APP_CMD")
    m.call("WndOpen")
    m.poke(m.sym("CmdBk1"), m.sym("ST_RAM"))
    m.m.b = ord("C")
    m.call("HdlKey")
    left, right = panes(m)
    if left[0] != "BANK":
        failures.append("a 128K's left pane is not the banks")
        print(f"  left is {left}")
    elif right != ("RAM", ["XFER"]):
        failures.append("a copy between two devices does not arrive")
        print(f"  right is {right}")
    else:
        print(f"  on a 128K the panes are {left[0]} and {right[0]}, and C "
              f"copies XFER from one device to the other through E1")
    return failures


def desktop_checks():
    """Shortcuts on the desktop, which is the GEM idea taken literally.

    Three things have to be true and none of them is the picture. A
    press and a release in the same place opens the shortcut; a press
    and a drag moves it; and an icon a window was dragged over comes
    back, because the strip a drag vacates is filled with lattice and
    whatever was showing through it has to be put back.
    """
    failures = []
    m = boot(text="")
    print("desktop shortcuts:")
    SZ = m.sym("DSKENTSZ")

    def ent(i, off):
        return m.peek(m.sym("DskTab") + i * SZ + off)

    def press(px, py):
        m.poke(m.sym("PtrX"), px)
        m.poke(m.sym("PtrY"), py)
        m.poke(m.sym("Buttons"), 0xFD)
        m.call("EvPoll")
        m.call("EvDispatch")

    def move(px, py):
        m.poke(m.sym("PtrX"), px)
        m.poke(m.sym("PtrY"), py)
        m.call("HdlPtrMove")

    def release():
        m.poke(m.sym("Buttons"), 0xFF)
        m.call("EvPoll")
        m.call("EvDispatch")

    # the icon is really on the screen, as pixels
    rows = [(m.peek(m.scr_addr(1, 16 + r)), m.peek(m.scr_addr(2, 16 + r)))
            for r in range(16)]
    if rows[0] != (0xFF, 0xFF) or rows[15] != (0xFF, 0xFF) or rows[3] != (0xBF, 0xFD):
        failures.append("the first shortcut is not drawn on the desktop")
        print(f"  rows 0, 3, 15: {rows[0]}, {rows[3]}, {rows[15]}")
    else:
        print("  the document icon is on the desktop: a sixteen pixel frame "
              "with four lines written in it")

    m.m.bc = (1 << 8) | 50
    m.call("DskHit")
    hit = m.m.a
    m.m.bc = (10 << 8) | 50
    m.call("DskHit")
    miss = m.m.a
    if hit != 2 or miss != 0:
        failures.append("the icon hit test is wrong")
        print(f"  on the clock {hit}, on bare desktop {miss}")
    else:
        print("  a press lands on the clock icon and not on the desktop "
              "beside it")

    before = m.peek(m.sym("WndCount"))
    press(12, 50)
    release()
    if m.peek(m.sym("WndCount")) != before + 1:
        failures.append("a press and a release does not open the shortcut")
    else:
        print("  press and release in one place opens it, no double click "
              "timer on a machine most of whose owners had no mouse")

    m = boot(text="")
    press(12, 50)
    move(100, 100)
    move(120, 110)
    release()
    if (ent(1, 1), ent(1, 2)) != (15, 110):
        failures.append("a press and a drag does not move the shortcut")
        print(f"  the clock icon is at {(ent(1, 1), ent(1, 2))}")
    else:
        print("  press and drag moves it instead, decided by three pixels "
              "of travel rather than by a clock")

    # a window dragged over an icon must not eat it
    m = boot(text="")
    m.poke(m.sym("WinX"), 8)
    m.poke(m.sym("WinY"), 48)
    m.call("WndRepaintAll")
    want = [(m.peek(m.scr_addr(1, 48 + r)), m.peek(m.scr_addr(2, 48 + r)))
            for r in range(16)]
    m.poke(m.sym("WinOldX"), 8)
    m.poke(m.sym("WinX"), 3)                # drag it left, over the icons
    m.poke(m.sym("WinMoved"), 1)
    m.call("WinRedraw")
    m.poke(m.sym("WinOldX"), 3)
    m.poke(m.sym("WinX"), 8)                # and back off them again
    m.poke(m.sym("WinMoved"), 1)
    m.call("WinRedraw")
    got = [(m.peek(m.scr_addr(1, 48 + r)), m.peek(m.scr_addr(2, 48 + r)))
           for r in range(16)]
    if got != want:
        failures.append("a window dragged over a shortcut erases it")
        print(f"  row 0 was {want[0]} and is {got[0]}")
    else:
        print("  a window dragged across the clock icon and off it again "
              "leaves the icon exactly as it was")

    # A window standing over the icons, and another dragged past. The
    # icon is painted whole rather than clipped to the strip, so the
    # part of it reaching outside used to land on the window: CLOCK
    # appeared on top of the file manager while CAL and FILES beside it
    # were correctly behind it.
    m = boot(text="")
    m.m.a = m.sym("APP_CMD")
    m.call("WndOpen")                   # opens at column two, over the
    # The pointer goes somewhere neither the window nor an icon reaches.
    # WinRedraw ends by drawing it and WndRepaintAll does not, so leaving
    # it where it starts makes the two differ by eleven rows of arrow and
    # says nothing about either.
    m.poke(m.sym("PtrX"), 200)
    m.poke(m.sym("PtrY"), 170)
    m.call("WndRepaintAll")
    m.call("PtrSaveBg")
    m.call("PtrDraw")

    def screen():
        return bytes(m.peek(0x4000 + i) for i in range(6144))

    want = screen()
    m.poke(m.sym("WinOldY"), m.peek(m.sym("WinY")))
    m.poke(m.sym("WinY"), m.peek(m.sym("WinY")) + 2)
    m.poke(m.sym("WinMoved"), 1)
    m.call("WinRedraw")
    m.poke(m.sym("WinOldY"), m.peek(m.sym("WinY")))
    m.poke(m.sym("WinY"), m.peek(m.sym("WinY")) - 2)
    m.poke(m.sym("WinMoved"), 1)
    m.call("WinRedraw")
    got = screen()
    if got != want:
        bad = [i for i in range(6144) if got[i] != want[i]]
        failures.append("a drag past a window standing over the icons "
                        "paints desktop over the window")
        print(f"  {len(bad)} bytes differ, first at {hex(0x4000 + bad[0])}")
    else:
        print("  a window standing over the labels stays intact when "
              "another is dragged past: the strip grows to cover whatever "
              "icon was put back, and the windows go over all of it")

    # and the arrangement survives being put away and fetched back
    m = boot(text="")
    m.poke(m.sym("DskTab") + 1 * SZ + 1, 20)
    m.poke(m.sym("DskTab") + 1 * SZ + 2, 96)
    m.poke(m.sym("DskTab") + 3 * SZ, 0)
    m.call("DskSave")
    m.call("DskInit")
    m.call("DskLoad")
    if (ent(1, 1), ent(1, 2), ent(3, 0)) != (20, 96, 0):
        failures.append("the desktop arrangement does not survive a save")
        print(f"  {(ent(1, 1), ent(1, 2), ent(3, 0))}")
    else:
        print("  where they were put and which are shown both come back "
              "through the storage layer, so a desktop does not forget "
              "itself")
    return failures


def close_checks():
    """The close vector, which F2 deliberately did not build.

    F2's argument was that everything an application allocates comes
    back through the owner byte, so nothing has to be asked. That is
    still true and this is not about memory: it is about consent. A
    notepad with unsaved work has something to lose and the system had
    no way for it to say so.

    Carry set means "not yet" rather than "no". The panel system is
    not a modal loop, so the question cannot block; the close is
    refused, and the answer performs it.
    """
    failures = []
    print("closing an application:")

    def opened(text):
        m = boot(text="")
        m.m.a = m.sym("APP_NOTE")
        m.call("WndOpen")
        for ch in text:
            m.m.b = ord(ch)
            m.call("HdlKey")
        return m

    def key(m, k):
        m.m.b = k
        m.call("HdlKey")

    m = opened("")
    before = m.peek(m.sym("WndCount"))
    m.call("WndClose")
    if m.peek(m.sym("WndCount")) != before - 1:
        failures.append("a notepad with nothing in it will not close")
    elif m.peek(m.sym("DgDepth")):
        failures.append("a clean notepad asks before closing")
    else:
        print("  a notepad with nothing to lose closes without asking")

    # the about window and the clock have no close vector at all
    m = boot(text="")
    for app in ("APP_ABOUT", "APP_CLOCK"):
        m.m.a = m.sym(app)
        m.call("WndOpen")
        n = m.peek(m.sym("WndCount"))
        m.call("WndClose")
        if m.peek(m.sym("WndCount")) != n - 1:
            failures.append(f"{app} will not close")
    print("  an application with no close vector consents by having none")

    m = opened("KEEP ME")
    if not m.peek(m.sym("NoteModified")):
        failures.append("typing does not mark the document modified")
    m.call("WndClose")
    if m.peek(m.sym("WndCount")) != 2 or not m.peek(m.sym("DgDepth")):
        failures.append("a modified notepad closes without asking")
        print(f"  {m.peek(m.sym('WndCount'))} windows, "
              f"dialogue depth {m.peek(m.sym('DgDepth'))}")
    else:
        print("  one with unsaved work refuses and puts the question up "
              "instead")

    ENTER, DOWN = m.sym("KEY_ENTER"), m.sym("KEY_DOWN")
    key(m, ENTER)                           # CANCEL, where the bar starts
    if m.peek(m.sym("WndCount")) != 2 or not m.peek(m.sym("NoteModified")):
        failures.append("CANCEL does not leave the window alone")
    else:
        print("  CANCEL is the row the bar starts on, and it changes nothing")

    m = opened("KEEP ME")
    m.call("WndClose")
    key(m, DOWN)
    key(m, ENTER)                           # DISCARD
    if m.peek(m.sym("WndCount")) != 1:
        failures.append("DISCARD does not close the window")
    else:
        print("  DISCARD closes it and the work is gone, which is what it "
              "says")

    m = opened("KEEP ME")
    m.call("WndClose")
    key(m, DOWN)
    key(m, DOWN)
    key(m, ENTER)                           # SAVE
    if m.peek(m.sym("WndCount")) != 1:
        failures.append("SAVE does not close the window")
    elif m.peek(m.sym("NoteModified")):
        failures.append("SAVE closes without having saved")
    else:
        print("  SAVE writes it through the storage layer first, and the "
              "window goes only once that has worked")

    # and the document really is on disc: read it back into a new one
    m.m.a = m.sym("APP_NOTE")
    m.call("WndOpen")
    m.call("NoteLoad")
    base, cols = m.sym("NoteBuf"), m.sym("NOTECOLS")
    got = "".join(chr(m.peek(base + c)) for c in range(cols)).rstrip()
    if got != "KEEP ME":
        failures.append("what SAVE wrote is not what comes back")
        print(f"  the file reads {got!r}")
    else:
        print(f"  and a fresh notepad loads {got!r} back off it")
    return failures


def clock_checks():
    """The clock, and the only thing it can be wrong about.

    It has no crystal to be right about, so what is checkable is the
    arithmetic: that a second is the right number of interrupts. The
    48K interrupt is 3500000/69888 = 50.0801 Hz, not 50, and a clock
    that ticks every fifty gains two minutes eighteen a day.

    There is no interrupt source here, so IrqCnt is driven by hand.
    That is also what the clock reads, which is the point of it
    reading IrqCnt rather than counting its own frames.
    """
    failures = []
    m = boot(text="")
    m.call("ClkInit")
    print("clock:")

    def hms():
        return tuple(m.peek(m.sym(n)) for n in ("ClkH", "ClkM", "ClkS"))

    def advance(n=1):
        m.poke16(m.sym("IrqCnt"), (m.peek16(m.sym("IrqCnt")) + n) & 0xFFFF)
        m.call("ClkTick")

    if hms() != (12, 0, 0):
        failures.append("the clock does not start at noon")
    # the face shows hours and minutes only, so a second is not a repaint
    m.poke(m.sym("ClkDirty"), 0)
    advance(50)
    if m.peek(m.sym("ClkDirty")):
        failures.append("a second asks the clock to repaint")
    else:
        print("  a second passes without asking for a repaint, which is what "
              "dropping them from the face was for")
    if m.peek(m.sym("ClkFracAdd")) != m.sym("CLKFRAC48"):
        failures.append("the 48K correction is not the one detected")

    m.call("ClkInit")                       # the check above spent a second
    interrupts = 0
    while hms() != (13, 0, 0) and interrupts < 200_000:
        advance()
        interrupts += 1
    true_hour = round(3600 * 3500000 / 69888)
    drift = abs(interrupts - true_hour)
    if hms() != (13, 0, 0):
        failures.append("the clock did not reach one o'clock")
    elif drift > 4:
        failures.append("the clock is out by more than four interrupts an hour")
        print(f"  an hour took {interrupts}, a true hour is {true_hour}")
    else:
        secs_a_day = drift * 24 * 3600 / true_hour
        print(f"  an hour is {interrupts} interrupts against a true "
              f"{true_hour}: {secs_a_day:.1f} seconds a day, where ticking "
              f"every fifty would be {(true_hour - 180000) * 24 / 50:.0f}")

    # interrupts the loop never saw still advance it, which is what
    # reading A3's counter rather than counting frames is for
    m.call("ClkInit")
    advance(500)                            # ten seconds in one go
    if hms()[:2] != (12, 0) or hms()[2] not in (9, 10):
        failures.append("a burst of interrupts does not advance the clock")
        print(f"  five hundred interrupts left it at {hms()}")
    else:
        print(f"  five hundred interrupts arriving at once put it at "
              f"{hms()[1]:02d}:{hms()[2]:02d}, so a frame the main loop "
              f"never reached is not a second lost")

    # midnight moves the date, which is the one thing the date does
    # not need a person for
    m.poke(m.sym("TodayY"), 46)
    m.poke(m.sym("TodayM"), 11)
    m.poke(m.sym("TodayD"), 31)
    m.poke(m.sym("ClkH"), 23)
    m.poke(m.sym("ClkM"), 59)
    m.poke(m.sym("ClkS"), 59)
    for _ in range(52):
        advance()
    today = tuple(m.peek(m.sym(n)) for n in ("TodayY", "TodayM", "TodayD"))
    if hms()[0] != 0 or today != (47, 0, 1):
        failures.append("midnight on new year's eve does not roll the date")
        print(f"  clock {hms()}, today {today}")
    else:
        print("  midnight on 31 December 2026 rolls to 1 January 2027")

    # setting it, which needs a clock window in front: HdlKey goes to
    # whatever owns that, and after boot it is the notepad
    m.call("ClkInit")
    m.m.a = m.sym("APP_CLOCK")
    m.call("WndOpen")
    m.m.b = m.sym("KEY_RIGHT")
    m.call("HdlKey")                        # onto the minutes
    m.m.b = m.sym("KEY_UP")
    m.call("HdlKey")
    if hms()[:2] != (12, 1) or m.peek(m.sym("ClkField")) != 1:
        failures.append("the keys do not set the clock")
        print(f"  {hms()}, field {m.peek(m.sym('ClkField'))}")
    else:
        print("  right then up moves to the minutes and changes them alone")
    for _ in range(59):
        m.m.b = m.sym("KEY_UP")
        m.call("HdlKey")
    if hms()[:2] != (12, 0):
        failures.append("the minutes do not wrap within themselves")
        print(f"  after sixty ups: {hms()}")
    else:
        print("  and sixty of them wrap the minutes without touching the hour")
    return failures


def calendar_checks():
    """The calendar, checked against a century of real dates.

    Day of the week is counted forward from 1 January 1980, a
    Tuesday, because Zeller and Sakamoto both want division by 4, 100
    and 400 and the Z80 has no divide. Every month it can show is
    compared here with what Python makes of the same date, which is
    the strongest check available: 1,200 months and no opinion in
    any of them.
    """
    import datetime
    failures = []
    m = boot(text="")
    print("calendar:")

    wrong = []
    for year in range(1980, 2080):
        for month in range(12):
            m.poke(m.sym("CalYear"), year - 1980)
            m.poke(m.sym("CalMonth"), month)
            m.call("CalFirstDow")
            got = m.peek(m.sym("CalDow"))
            want = datetime.date(year, month + 1, 1).weekday()
            if got != want:
                wrong.append((year, month + 1, got, want))
            m.call("CalSetLeap")
            m.m.a = month
            m.call("CalMonthLen")
            first = datetime.date(year, month + 1, 1)
            nxt = (datetime.date(year + 1, 1, 1) if month == 11
                   else datetime.date(year, month + 2, 1))
            if m.m.a != (nxt - first).days:
                wrong.append((year, month + 1, "len", m.m.a))
    if wrong:
        failures.append("the day of the week or a month length is wrong")
        print(f"  {len(wrong)} wrong, first few: {wrong[:4]}")
    else:
        print("  every month from 1980 to 2079 agrees with Python's calendar "
              "on both the first weekday and the length: 1,200 of them, "
              "leap years and all")

    # 2000 is the year the century rule would have got wrong
    m.poke(m.sym("CalYear"), 20)
    m.call("CalSetLeap")
    m.m.a = 1
    m.call("CalMonthLen")
    if m.m.a != 29:
        failures.append("February 2000 is not 29 days")
    else:
        print("  February 2000 is 29 days, which is the one the century rule "
              "would have got wrong if this range needed one")

    # the grid, read back off the screen for a month whose shape is known
    m = boot(text="")
    m.poke(m.sym("TodayY"), 46)
    m.poke(m.sym("TodayM"), 7)
    m.poke(m.sym("TodayD"), 30)
    m.m.a = m.sym("APP_CAL")
    m.call("WndOpen")
    font = {}
    for code in range(32, 128):
        font[m.font_glyph(chr(code))] = chr(code)
        font[m.font_glyph(chr(code), invert=True)] = chr(code)
    x = m.peek(m.sym("WinX")) + 1
    y = m.peek(m.sym("WinY")) + m.sym("WinCapH")

    def text(px, n):
        return "".join(font.get(m.glyph_at(x + i, px), "?") for i in range(n))

    head = text(y + 3, 8)
    days = text(y + 12, 20)
    weeks = [text(y + 21 + w * 8, 20) for w in range(6)]
    want = ["                1  2", " 3  4  5  6  7  8  9",
            "10 11 12 13 14 15 16", "17 18 19 20 21 22 23",
            "24 25 26 27 28 29 30", "31                  "]
    if head != "AUG 2026" or days != "MO TU WE TH FR SA SU" or weeks != want:
        failures.append("the month grid is not what August 2026 looks like")
        print(f"  {head!r} {days!r}")
        for w in weeks:
            print(f"    {w!r}")
    else:
        print("  August 2026 on screen: the 1st on a Saturday, the 31st on a "
              "Monday, six rows, and the labels over the digits")

    # the selection walks across a month boundary, which is the only
    # part of moving a day that is not an increment
    m.poke(m.sym("CalSel"), 1)
    m.m.b = m.sym("KEY_LEFT")
    m.call("HdlKey")
    got = (m.peek(m.sym("CalYear")), m.peek(m.sym("CalMonth")),
           m.peek(m.sym("CalSel")))
    if got != (46, 6, 31):
        failures.append("stepping back off the 1st does not enter July")
        print(f"  landed on {got}")
    else:
        print("  a step back from 1 August lands on 31 July, not on nothing")
    m.m.b = m.sym("KEY_RIGHT")
    m.call("HdlKey")
    m.m.b = m.sym("KEY_ENTER")
    m.call("HdlKey")
    today = tuple(m.peek(m.sym(n)) for n in ("TodayY", "TodayM", "TodayD"))
    if today != (46, 7, 1):
        failures.append("ENTER does not set the date")
        print(f"  today is {today}")
    else:
        print("  and ENTER makes the selected day today, which is the only "
              "way this machine has of being told the date")
    return failures


def bank_checks():
    """F3. The 128K's spare banks as a RAM disk.

    The harness models paging for this, because a backend that can
    only be run in Fuse is a backend nothing asserts on. What it
    cannot model is contention or the real ULA, so the Fuse run still
    matters; what it can model is every question F3 actually raises.

    The sharpest of those is the one the state document has been
    carrying as a hazard since the buffers moved: every buffer in this
    system lives at $C000, which on a 128K is the bank being paged. If
    a transfer leaves the wrong bank selected, or pages while holding
    a pointer into bank 0, the window buffer and the notepad go with
    it.
    """
    failures = []

    def machine(banked):
        mm = Machine("build/zxdesk.bin", "build/zxdesk.sym", banked=banked)
        for r in ("DetectMachine", "SetupIM2", "BuildScrTab", "MouseInit",
                  "JoyInit", "StInit"):
            mm.call(r)
        return mm

    print("F3 banking backend:")
    m48 = machine(False)
    if m48.peek(m48.sym("Is128")) != 0 or             m48.peek(m48.sym("StBackend")) != m48.sym("ST_RAM"):
        failures.append("a 48K does not get the RAM backend")
        print(f"  48K: Is128 {m48.peek(m48.sym('Is128'))}, backend "
              f"{m48.peek(m48.sym('StBackend'))}")
    m = machine(True)
    if m.peek(m.sym("Is128")) != 1 or             m.peek(m.sym("StBackend")) != m.sym("ST_BANK"):
        failures.append("a 128K does not get the banking backend")
        print(f"  128K: Is128 {m.peek(m.sym('Is128'))}, backend "
              f"{m.peek(m.sym('StBackend'))}")
    else:
        print("  the same StInit picks RAM on a 48K and the banks on a 128K")

    # every buffer is at $C000, which is the bank being paged
    marks = {"RamHeap": 0x11, "SuArena": 0x22, "NoteBuf": 0x33,
             "HeapBase": 0x44}
    for name, val in marks.items():
        for i in range(16):
            m.poke(m.sym(name) + i, val)

    def named(text):
        for i, ch in enumerate(text):
            m.poke(m.sym("FmName") + i, ord(ch))
        m.poke(m.sym("FmName") + len(text), 0)
        return m.sym("FmName")

    def write(text, src, count):
        m.m.hl = named(text)
        m.m.b = m.sym("FA_OVERWRITE")
        m.call("StOpen")
        if m.m.f & 1:
            return None
        h = m.m.a
        m.m.hl = src
        m.m.bc = count
        m.call("StWrite")
        n = m.m.bc
        m.m.a = h
        m.call("StClose")
        return n

    def read(text, dst, count):
        m.m.hl = named(text)
        m.m.b = m.sym("FA_READ")
        m.call("StOpen")
        if m.m.f & 1:
            return None
        h = m.m.a
        m.m.hl = dst
        m.m.bc = count
        m.call("StRead")
        n = m.m.bc
        m.m.a = h
        m.call("StClose")
        return n

    # a file far larger than the 48K backend's whole 256 byte chunk,
    # written from a buffer that is itself above $C000
    src, dst, N = m.sym("SuArena") + 256, m.sym("HeapBase") + 4096, 3000
    for i in range(N):
        m.poke(src + i, (i * 7) & 0xFF)
        m.poke(dst + i, 0)
    wrote = write("BIGFILE", src, N)
    got = read("BIGFILE", dst, N)
    bad = sum(1 for i in range(N) if m.peek(dst + i) != ((i * 7) & 0xFF))
    print(f"  a {N} byte file, written and read from buffers that are "
          f"themselves in the paged bank: wrote {wrote}, read {got}, "
          f"{bad} bytes wrong")
    if wrote != N or got != N or bad:
        failures.append("a large file does not round trip through the banks")

    intact = [n for n, v in marks.items() if m.peek(m.sym(n)) != v]
    if intact:
        failures.append("paging ate a buffer")
        print(f"  these were paged out and not brought back: {intact}")
    else:
        print("  every buffer at $C000 survives, which is the hazard this "
              "package was carrying")

    # a file bigger than its chunk is clamped rather than running into
    # the next one, which on a bank boundary would be another file
    m.m.hl = named("HUGE")
    m.m.b = m.sym("FA_OVERWRITE")
    m.call("StOpen")
    if m.m.f & 1:
        failures.append("could not create a file to test the chunk limit")
    else:
        h, total = m.m.a, 0
        for _ in range(6):
            m.m.hl = src
            m.m.bc = 2000
            m.call("StWrite")
            total += m.m.bc
        m.m.a = h
        m.call("StClose")
        if total != m.sym("BKCHUNK"):
            failures.append("a file is not clamped to its chunk")
            print(f"  writing 12,000 bytes into an {m.sym('BKCHUNK')} byte "
                  f"file stored {total}")
        else:
            print(f"  writing past the end of a file stops at {total} bytes "
                  f"rather than running into the next one")

    # capacity, and what happens past it. Everything so far is deleted
    # first, which also checks delete frees a slot rather than only
    # hiding a name.
    for gone in ("BIGFILE", "HUGE"):
        m.m.hl = named(gone)
        m.call("StDelete")
    made = 0
    for i in range(m.sym("BANKFILES") + 2):
        if write(f"F{i}", src, 64) is not None:
            made += 1
    if made != m.sym("BANKFILES"):
        failures.append("the directory does not hold what it says")
        print(f"  {made} files were created, expected {m.sym('BANKFILES')}")
    else:
        print(f"  after deleting two, it takes exactly "
              f"{m.sym('BANKFILES')} files and refuses the rest, so delete "
              f"frees the slot")
    return failures


def mouse_checks():
    """The Kempston mouse, driven through its own ports.

    This exists because the pointer would not move in Fuse and the
    cause was invisible from outside: ClampDelta discarded any delta
    at or beyond 25, on the grounds that a floating port produces
    large random swings. It does, and so does a mouse. The MOUSETEST
    build measured 68 in one frame on a real run, and everything from
    25 up was being thrown away in full, so the pointer moved if you
    crept and sat still if you moved it normally.

    The pair of reads that have to agree is the actual defence
    against a floating port. The clamp's job is to bound movement,
    not to reject it.
    """
    failures = []
    m = Machine("build/zxdesk.bin", "build/zxdesk.sym")
    port = {"x": 0x80, "y": 0x80, "b": 0xFF}
    m.m.set_input_callback(
        lambda a: port["x"] if (a & 0xFFFF) == 0xFBDF else
                  port["y"] if (a & 0xFFFF) == 0xFFDF else
                  port["b"] if (a & 0xFFFF) == 0xFADF else 0xFF)
    for r in ("DetectMachine", "SetupIM2", "BuildScrTab", "MouseInit",
              "JoyInit", "StInit", "SetLoad", "SetApply"):
        m.call(r)

    print("Kempston mouse:")
    if m.peek(m.sym("MouseOn")) != 1:
        failures.append("a mouse answering its ports is not detected")
        print("  MouseInit did not detect a mouse that answers all three ports")
    else:
        print("  a mouse answering $FBDF, $FFDF and $FADF is detected")

    cap = m.sym("MOUSEMAX")

    def move(delta):
        port["x"] = 0x80
        port["y"] = 0x80
        m.poke(m.sym("MouseLX"), 0x80)
        m.poke(m.sym("MouseLY"), 0x80)
        m.poke(m.sym("PtrX"), 120)
        m.poke(m.sym("PtrY"), 90)
        port["x"] = (0x80 + delta) & 0xFF
        port["y"] = (0x80 + delta) & 0xFF
        m.call("ReadInput")
        return (m.peek(m.sym("PtrX")) - 120, m.peek(m.sym("PtrY")) - 90)

    bad = []
    for d in (1, 4, 10, 24, 25, 26, 40, 63, 64, 68, 100, 127,
              -1, -24, -25, -40, -64, -68, -100):
        dx, dy = move(d)
        want = max(-cap, min(cap, d))
        if dx != want or dy != -want:      # Y is inverted by default
            bad.append((d, dx, dy, want))
    if bad:
        failures.append("the mouse delta clamp is wrong")
        for d, dx, dy, want in bad[:6]:
            print(f"  a delta of {d} moved the pointer {dx},{dy}, "
                  f"expected {want},{-want}")
    else:
        print(f"  every delta up to the {cap} pixel cap moves the pointer by "
              f"exactly itself, and past it by the cap")
    if move(25)[0] == 0:
        failures.append("a delta of 25 is still discarded")
        print("  25 still moves the pointer nowhere, which was the bug")

    # a floating port cannot give the same value twice, and that is what
    # the two reads in ReadInput are for
    m.poke(m.sym("PtrX"), 120)
    flip = [0]

    def floating(a):
        if (a & 0xFFFF) in (0xFBDF, 0xFFDF):
            flip[0] ^= 0xFF                 # never the same value twice
            return flip[0]
        return 0xFF
    m.m.set_input_callback(floating)
    for _ in range(8):
        m.call("ReadInput")
    if m.peek(m.sym("PtrX")) != 120:
        failures.append("a port that never reads the same twice moves the pointer")
        print(f"  a floating port moved the pointer to {m.peek(m.sym('PtrX'))}")
    else:
        print("  a port that never reads the same twice is ignored, which is "
              "what the clamp no longer has to do")
    return failures


def compositor_checks():
    """B4. Two windows, and the questions two windows make real.

    The blit sweep at the end is the interesting one, and it is a
    regression check for a bug that was in RectBlit from the day it
    was written. Its row step subtracted the width from E alone and
    then incremented D regardless, which puts the next row a page too
    high whenever a rectangle's row start plus its width crosses 256.
    The only window in the system lived at column 8 and was eight
    bytes short of it, so nothing ever crossed. B4 moved a window to
    column 16 and it appeared at once, as four rows painted below the
    window.
    """
    import hashlib
    failures = []

    def two(text="TWO WINDOWS"):
        mm = boot(text=text)
        mm.poke(mm.sym("PtrX"), 240)        # park the pointer off both windows
        mm.poke(mm.sym("PtrY"), 20)
        mm.call("PtrSaveBg")
        mm.call("PtrDraw")
        mm.call("AboutOpen")
        return mm

    def zorder(mm):
        return [mm.peek(mm.sym("WndZ") + i)
                for i in range(mm.peek(mm.sym("WndCount")))]

    def cap(mm, win):
        """A blank part of a window's title bar, past the end of its text.

        Solid means the window is in front. The middle of the bar is no
        good: the title is printed there and its glyphs are ink either
        way round.
        """
        sz, tab = mm.sym("WNDRECSZ"), mm.sym("WndTab")
        x, y = mm.peek(tab + win * sz), mm.peek(tab + win * sz + 1)
        w = mm.peek(tab + win * sz + 2)
        return mm.peek(mm.scr_addr(x + w - 2, y + 4))

    print("B4 compositor:")
    m = two()
    if m.peek(m.sym("WndCount")) != 2 or zorder(m) != [1, 0]:
        failures.append("opening a second window is wrong")
        print(f"  count {m.peek(m.sym('WndCount'))} z {zorder(m)}")
    elif cap(m, 1) == 0 or cap(m, 0) != 0:
        failures.append("the title bars do not say which window is in front")
        print(f"  about cap {cap(m, 1):02X}, notes cap {cap(m, 0):02X}")
    else:
        print("  two windows: the new one is in front, and the one behind "
              "has its title bar withheld rather than filled")

    m.m.b, m.m.c = 10, 50                   # the notepad's title bar
    m.call("WndHitTest")
    part, which = m.m.a, m.peek(m.sym("WndHit"))
    if part != 2 or which != 0:
        failures.append("the window hit test does not walk the z order")
        print(f"  hit part {part} window {which}, expected a title bar on 0")
    m.m.a = which
    m.call("WndFocusTo")
    if zorder(m) != [0, 1] or cap(m, 0) == 0 or cap(m, 1) != 0:
        failures.append("raising a window does not swap the focus")
        print(f"  z {zorder(m)} notes {cap(m, 0):02X} about {cap(m, 1):02X}")
    else:
        print("  pressing the one behind raises it, and the bars swap over")

    # a key goes to the front window, and the about window has nowhere
    # to put one
    m.m.b = ord("Z")
    m.call("HdlKey")
    base, stride = m.sym("NoteBuf"), m.sym("NOTESTRIDE")
    if chr(m.peek(base + 11)) != "Z":
        failures.append("a key does not reach the front window")
    m.m.a = 1
    m.call("WndFocusTo")                    # about to the front
    m.m.b = ord("Q")
    m.call("HdlKey")
    if chr(m.peek(base + 12)) == "Q":
        failures.append("a key reaches a window that is not in front")
        print("  a key typed with the about window in front reached the note")
    else:
        print("  a key goes to the front window, and the about window drops it")
    m.m.a = 0
    m.call("WndFocusTo")

    # dragging one window across the other has to land where a full
    # repaint would
    addr = {}
    for r in range(192):
        for c in range(32):
            addr[m.scr_addr(c, r) - 0x4000] = (c, r)

    def pointer(c, r):
        return 29 <= c <= 31 and 18 <= r <= 32

    def pixels():
        return bytes(m.peek(0x4000 + i) for i in range(6144))

    wrong = 0
    moves = [(1, 2)] * 8 + [(0, 3)] * 4 + [(-1, -2)] * 6 + [(0, -4)] * 3
    for dx, dy in moves:
        m.poke(m.sym("WinOldX"), m.peek(m.sym("WinX")))
        m.poke(m.sym("WinOldY"), m.peek(m.sym("WinY")))
        # Clamped to the screen, because this pokes the record rather
        # than going through DdMove, which is where the real drag does
        # its clamping. The notepad grew two columns when it got a
        # scroll bar and these moves then walked its right edge off the
        # screen, where a one column damage strip is filled at column 32
        # and wraps into another row entirely. That is worth knowing and
        # it is not what this check is about.
        nx = min(max(m.peek(m.sym("WinX")) + dx, 0),
                 32 - m.peek(m.sym("WinW")))
        m.poke(m.sym("WinX"), nx)
        m.poke(m.sym("WinY"), m.peek(m.sym("WinY")) + dy)
        m.poke(m.sym("WinMoved"), 1)
        m.call("WinRedraw")
        got = pixels()
        m.call("WndRepaintAll")
        bad = [addr[i] for i in range(6144)
               if got[i] != m.peek(0x4000 + i) and not pointer(*addr[i])]
        if bad:
            wrong += 1
            if wrong == 1:
                print(f"  first difference at {bad[:4]}")
    print(f"  {len(moves)} drag steps across the other window, each compared "
          f"with a full repaint: {wrong} wrong")
    if wrong:
        failures.append("a drag over another window paints the wrong thing")

    # And the blit's row step, at every position a window can be in.
    # One machine for the whole sweep: booting a fresh one per case was
    # three hundred and sixty boots and turned a five second suite into
    # a five minute one, which is a suite that stops being run.
    mm = boot(text="X")
    rowaddr = [[mm.scr_addr(c, r) for c in range(32)] for r in range(192)]

    def rows_touched():
        mm.call("InitScreen")
        before = bytes(mm.peek(0x4000 + i) for i in range(6144))
        mm.call("BlitRect")
        after = bytes(mm.peek(0x4000 + i) for i in range(6144))
        return [r for r in range(192)
                if any(before[a - 0x4000] != after[a - 0x4000]
                       for a in rowaddr[r])]

    off = cases = 0
    h = mm.peek(mm.sym("WinH"))
    # Up to WINMAXW, not up to sixteen. The sweep stopped at sixteen and
    # RectBlit's unrolled LDI chain was twenty four long, so a window
    # wider than that entered the chain before it started and ran
    # whatever was in front of it. Nothing was wider than sixteen until
    # the file manager wanted thirty, and then the machine hung.
    for w in (2, 6, 10, 16, 24, 26, 30, 32):
        for x in range(0, 33 - w, 2):
            for y in (48, 56, 60, 64, 67, 100):
                if y + h > 192:
                    continue
                cases += 1
                mm.poke(mm.sym("WinW"), w)
                mm.poke(mm.sym("WinX"), x)
                mm.poke(mm.sym("WinY"), y)
                mm.call("WinDraw")
                mm.call("WinGrab")
                rows = rows_touched()
                if not rows or rows[0] != y or rows[-1] != y + h - 1:
                    off += 1
                    print(f"  blit at {x},{y} {w} wide painted rows "
                          f"{rows[0] if rows else None}..."
                          f"{rows[-1] if rows else None}, expected {y}..{y+h-1}")
    print(f"  the blit paints exactly its own rows at all {cases} widths and "
          f"columns a window can have: {off} wrong")
    if off:
        failures.append("the blit's row step is wrong somewhere")
    return failures


def blitclip_checks():
    """B4 stage two. BlitClip against BlitRect, before anything uses it.

    BlitClip paints the intersection of a window with a damage
    rectangle, reading the matching sub rectangle of the window's
    buffer. It exists because repainting a whole window that the
    damage merely touches is about 13,000 T for this one, and a drag
    frame already costs 59,858 T of 69,888.

    It is checked here rather than through the compositor because a
    clipping bug seen through a drag looks like a damage bug, and the
    two would be debugged against each other. Every case compares
    against what BlitRect painted inside the intersection and against
    the bare desktop outside it, so a clip that paints too little and
    one that paints too much both fail.
    """
    failures = []

    def fresh():
        mm = Machine("build/zxdesk.bin", "build/zxdesk.sym")
        for r in ("DetectMachine", "SetupIM2", "BuildScrTab", "MouseInit",
                  "JoyInit", "StInit", "SetLoad", "SetApply", "DskInit",
                  "InitScreen", "WndInit"):
            mm.call(r)
        for ch in "COMPOSITOR":
            mm.call_with_a("NoteKey", ord(ch))
        mm.call("WinDraw")
        mm.call("WinGrab")
        return mm

    def pixels(mm):
        return bytes(mm.peek(0x4000 + i) for i in range(6144))

    ref = fresh()
    ref.call("InitScreen")
    clean = pixels(ref)                      # the desktop with no window on it
    ref.call("BlitRect")
    whole = pixels(ref)                      # and with the whole window on it
    wx, wy = ref.peek(ref.sym("WinX")), ref.peek(ref.sym("WinY"))
    ww, wh = ref.peek(ref.sym("WinW")), ref.peek(ref.sym("WinH"))
    addrs = [[ref.scr_addr(c, r) - 0x4000 for c in range(32)]
             for r in range(192)]

    m = fresh()

    def clip(cx, cy, cw, ch):
        m.call("InitScreen")
        for name, val in (("ClX", cx), ("ClY", cy), ("ClW", cw), ("ClH", ch)):
            m.poke(m.sym(name), val)
        m.call("BlitClip")
        return pixels(m)

    print("B4 clipped blit:")
    if clip(0, 0, 32, 192) != whole:
        failures.append("a clip covering the window does not match BlitRect")
        print("  a clip over the whole window paints something else")
    else:
        print("  a clip covering the whole window is identical to BlitRect")

    cases = wrong = 0
    for cx, cw in ((wx, 4), (wx + 2, 6), (wx + ww - 3, 3), (0, 32),
                   (wx - 2, ww + 4), (wx + ww, 4), (0, 2)):
        for cy, ch in ((wy, 8), (wy + 13, 20), (wy + wh - 5, 5), (0, 192),
                       (wy - 6, 6), (wy + wh, 10)):
            cases += 1
            got = clip(cx, cy, cw, ch)
            x0, y0 = max(wx, cx), max(wy, cy)
            x1, y1 = min(wx + ww, cx + cw), min(wy + wh, cy + ch)
            for r in range(192):
                for c in range(32):
                    ad = addrs[r][c]
                    inside = x0 <= c < x1 and y0 <= r < y1
                    if got[ad] != (whole[ad] if inside else clean[ad]):
                        wrong += 1
                        print(f"  clip {cx},{cy} {cw}x{ch} wrong at "
                              f"column {c} row {r}, "
                              f"{'inside' if inside else 'outside'}")
                        break
                else:
                    continue
                break
    print(f"  {cases} sub rectangles, including both edges, no overlap at "
          f"all, and larger than the window: {wrong} wrong")
    if wrong:
        failures.append("BlitClip disagrees with BlitRect")
    return failures


def surface_checks():
    """B3. A stack of transient surfaces instead of one buffer.

    The check is the thing that was impossible: open a menu, open a
    panel on top of it, and unwind. Every level has to come back byte
    for byte, in both directions, because a save under that restores
    approximately is worse than one that does not restore at all.

    One buffer had decided three separate things, and the third is
    checked here too: it was twelve byte columns wide, which capped a
    panel at twelve columns, which capped a filename below the ten
    characters a tape name has.
    """
    import hashlib
    failures = []
    m = boot(text="HELLO")
    m.poke(m.sym("PtrX"), 230)
    m.poke(m.sym("PtrY"), 150)
    m.call("PtrSaveBg")
    m.call("PtrDraw")

    def screen():
        return hashlib.sha256(
            bytes(m.peek(0x4000 + i) for i in range(6144))).hexdigest()[:12]

    def depth():
        return m.peek(m.sym("SuDepth"))

    print("B3 surface stack:")
    desktop = screen()
    m.m.a = 1                                   # the FILE menu
    m.call("MenuOpenDrop")
    menu = screen()
    if depth() != 1:
        failures.append("opening a menu did not push a surface")
    m.call("DlgOpen")                           # a panel on top of the menu
    if depth() != 2:
        failures.append("a panel over a menu did not push a second surface")
        print(f"  the stack is {depth()} deep, expected 2")
    else:
        print("  a menu and a panel over it are two surfaces, not a conflict")
    m.call("DlgClose")
    if screen() != menu or depth() != 1:
        failures.append("closing the panel does not restore the menu")
        print("  the menu did not come back exactly")
    else:
        print("  closing the panel brings the menu back byte for byte")
    m.call("MenuClose")
    if screen() != desktop or depth() != 0:
        failures.append("closing the menu does not restore the desktop")
        print("  the desktop did not come back exactly")
    else:
        print("  closing the menu brings the desktop back byte for byte")

    # the stack must refuse rather than corrupt when it runs out
    for n in range(m.sym("SUDEPTH") + 2):
        for name, val in (("SuX", 2), ("SuY", 100), ("SuW", 4), ("SuH", 8)):
            m.poke(m.sym(name), val)
        m.call("SaveUnder")
    if depth() != m.sym("SUDEPTH"):
        failures.append("the surface stack overruns its depth")
        print(f"  pushing {m.sym('SUDEPTH') + 2} left it {depth()} deep")
    else:
        print(f"  it refuses the push past {m.sym('SUDEPTH')} rather than "
              f"overrunning")
    while depth():
        m.call("RestoreUnder")
    m.call("RestoreUnder")                      # popping an empty stack
    if depth() != 0:
        failures.append("popping an empty surface stack underruns")

    # and the width limit that capped a filename
    if m.sym("FMFIELDLEN") < 10 or m.sym("RAMNAMESZ") < 11:
        failures.append("a filename is still shorter than a tape name")
        print(f"  the name box holds {m.sym('FMFIELDLEN')} and the backend "
              f"{m.sym('RAMNAMESZ') - 1}")
    else:
        print("  the name box and the backend both hold a ten character name")
    return failures


def dialog_checks():
    """D3, and the delete it exists for.

    A confirm opens over the file list rather than replacing it, which
    is what B3 and the panel record stack are between them for: if the
    answer is no, the list is still there and still usable.

    Three counts have to stay balanced through all of it, and getting
    one wrong is not a visible bug, it is a deaf panel. DlgUp counts
    how many panels are up; the surface stack holds the pixels under
    each; the record stack holds the panel each set of pixels belongs
    to. A confirm closing over a file list once reported that nothing
    was up, and the symptom would have been a list still on screen
    with every key going to the notepad behind it.
    """
    import hashlib
    failures = []
    m = boot(text="HELLO")
    m.poke(m.sym("PtrX"), 230)
    m.poke(m.sym("PtrY"), 150)
    m.call("PtrSaveBg")
    m.call("PtrDraw")

    def screen():
        return hashlib.sha256(
            bytes(m.peek(0x4000 + i) for i in range(6144))).hexdigest()[:12]

    def counts():
        return (m.peek(m.sym("DlgUp")), m.peek(m.sym("SuDepth")),
                m.peek(m.sym("PnlRecDep")))

    def rows():
        X = m.peek(m.sym("PnlX"))
        R0 = m.peek(m.sym("PnlRow0"))
        W = m.peek(m.sym("PnlW"))
        return ["".join(
            next((ch for ch in " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789?"
                  if m.font_glyph(ch) == m.glyph_at(X + c, R0 + i * 8)), "?")
            for c in range(1, W - 1)).rstrip()
            for i in range(m.peek(m.sym("PnlRows")))]

    print("D3 dialogues:")
    desktop = screen()
    for name in ("NOTE", "DRAFT"):
        for i, ch in enumerate(name):
            m.poke(m.sym("NoteName") + i, ord(ch))
        m.poke(m.sym("NoteName") + len(name), 0)
        m.call("NoteSave")

    m.call("FmDeletePanel")
    listing = screen()
    if rows() != ["NOTE", "DRAFT", "CANCEL"] or counts() != (1, 1, 0):
        failures.append("the delete list is wrong")
        print(f"  {rows()} {counts()}")
    m.m.c = m.sym("DLGROW0")
    m.call("PnlClick")
    got = rows()
    print(f"  pressing a row asks first: {got}")
    if got != ["DELETE FILE?", "NOTE", "NO", "YES"]:
        failures.append("the confirm does not name what it is deleting")
    if counts() != (2, 2, 1):
        failures.append("the confirm does not nest over the list")
        print(f"  counts are {counts()}, expected (2, 2, 1)")
    else:
        print("  it nests over the list: two panels, two surfaces, one record")
    if m.peek(m.sym("PnlFocus")) != 2:
        failures.append("the confirm does not default to NO")
        print("  the focus bar starts somewhere other than NO")
    else:
        print("  the focus bar starts on NO, so a hurried ENTER is the safe one")

    m.m.a = 13                                  # ENTER on NO
    m.call("DlgKey")
    if screen() != listing or counts() != (1, 1, 0):
        failures.append("answering no does not restore the list")
        print(f"  counts {counts()}, pixels back {screen() == listing}")
    else:
        print("  NO puts the list back byte for byte and the counts balance")

    m.m.c = m.sym("DLGROW0")
    m.call("PnlClick")
    m.m.a = 4                                   # down to YES
    m.call("DlgKey")
    m.m.a = 13
    m.call("DlgKey")
    got = rows()
    print(f"  YES deletes it and rebuilds the list: {got}")
    if got != ["DRAFT", "CANCEL"] or counts() != (1, 1, 0):
        failures.append("delete did not happen, or the counts leaked")
        print(f"  counts are {counts()}")
    m.call("FmClose")
    if screen() != desktop or counts() != (0, 0, 0):
        failures.append("the desktop does not come back after a delete")
        print(f"  counts {counts()}, pixels back {screen() == desktop}")
    else:
        print("  and closing the list leaves the desktop and all three "
              "counts at nought")

    # a tape cannot delete, and says so rather than pretending
    m.m.a = m.sym("ST_TAPE")
    m.call("StSelect")
    m.m.hl = m.sym("NoteName")
    m.call("StDelete")
    if not (m.m.f & 1):
        failures.append("the tape backend claims it deleted something")
    else:
        print("  a backend that cannot delete refuses rather than pretending")
    m.m.a = m.sym("ST_RAM")
    m.call("StSelect")

    # the alert, which has no answer to give
    m.m.hl = m.sym("TxtFmDelAsk")
    m.m.de = 0
    m.call("DlgAlert")
    if counts()[0] != 1 or rows()[2] != "OK":
        failures.append("the alert is wrong")
        print(f"  {rows()} {counts()}")
    else:
        print("  an alert is the same panel with one answer instead of two")
    m.m.a = 13
    m.call("DlgKey")
    if counts() != (0, 0, 0):
        failures.append("the alert does not unwind")
    return failures


def filemgr_checks():
    """G3, driven the way a person would drive it.

    A round trip through both panels: type into the note, save it
    under a name typed into a field, clear the note, find the file in
    the list, press its row, and get the text back.

    And the claim the design plan calls the signature element, which
    is the only one worth arguing about: the list is the storage layer
    rather than the RAM heap. Selecting the tape backend, which has no
    directory, has to empty the list without the file manager knowing
    what a tape is.
    """
    failures = []
    m = boot(text="")
    m.poke(m.sym("PtrX"), 230)              # park the pointer off the panel
    m.poke(m.sym("PtrY"), 150)
    m.call("PtrSaveBg")
    m.call("PtrDraw")
    X, R0 = m.sym("DLGX"), m.sym("DLGROW0")

    def cstr(addr, limit=12):
        out = ""
        for i in range(limit):
            ch = m.peek(addr + i)
            if ch == 0:
                break
            out += chr(ch)
        return out

    def note_row(r=0):
        base, stride = m.sym("NoteBuf"), m.sym("NOTESTRIDE")
        return "".join(chr(m.peek(base + r * stride + i))
                       for i in range(m.sym("NOTECOLS"))).rstrip()

    def rows():
        out = []
        for i in range(m.peek(m.sym("PnlRows"))):
            out.append("".join(
                next((ch for ch in " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
                      if m.font_glyph(ch) == m.glyph_at(X + c, R0 + i * 8)), "?")
                for c in range(1, m.peek(m.sym("PnlW")) - 1)).rstrip())
        return out

    def keys(seq):
        for k in seq:
            m.m.a = k if isinstance(k, int) else ord(k)
            m.call("PnlKey")

    print("G3 file manager:")
    m.call("FmOpenPanel")
    got = rows()
    print(f"  nothing saved yet: {got}")
    if got != ["NO FILES", "CANCEL"]:
        failures.append("the empty list is wrong")
    m.call("FmClose")

    for ch in "HELLO":
        m.call_with_a("NoteKey", ord(ch))
    m.call("FmSavePanel")
    if cstr(m.sym("FmField")) != "NOTE" or m.peek(m.sym("PnlFCur")) != 4:
        failures.append("the name box is not seeded with the current name")
        print(f"  the box holds {cstr(m.sym('FmField'))!r} with the cursor at "
              f"{m.peek(m.sym('PnlFCur'))}")
    else:
        print("  the name box is seeded with NOTE, cursor after it")
    keys([8] * 12)                          # clear it
    keys("SPECTRUM48")                      # ten characters, a tape name
    keys([4, 13])                           # down to OK, then ENTER
    if cstr(m.sym("NoteName")) != "SPECTRUM48" or m.peek(m.sym("FmUp")):
        failures.append("save as did not save, or did not close")
        print(f"  the note is now called {cstr(m.sym('NoteName'))!r}")
    else:
        print("  saved as SPECTRUM48, ten characters, and the note is "
              "now that file")

    m.call("NoteNew")
    if note_row():
        failures.append("NoteNew did not clear the note")
    m.call("FmOpenPanel")
    got = rows()
    print(f"  after the save: {got}")
    if got != ["SPECTRUM48", "CANCEL"]:
        failures.append("the saved file is not in the list")
    m.m.c = R0                              # press the first row
    m.call("PnlClick")
    if note_row() != "HELLO" or m.peek(m.sym("FmUp")):
        failures.append("opening a file from the list does not load it")
        print(f"  the note reads {note_row()!r} and the panel is still up")
    else:
        print("  pressing its row loads it back and closes the panel: "
              f"{note_row()!r}")

    # the list is the storage layer, not the RAM heap
    m.m.a = m.sym("ST_TAPE")
    m.call("StSelect")
    m.call("FmOpenPanel")
    got = rows()
    print(f"  with the tape backend selected: {got}")
    if got != ["NO FILES", "CANCEL"]:
        failures.append("the list does not follow the backend")
        print("  a backend with no directory should give an empty list")
    m.call("FmClose")
    m.m.a = m.sym("ST_RAM")
    m.call("StSelect")
    m.call("FmOpenPanel")
    if rows() != ["SPECTRUM48", "CANCEL"]:
        failures.append("selecting RAM again does not bring the list back")
    else:
        print("  selecting RAM again brings the same list back")
    m.call("FmClose")
    return failures


def loader_checks():
    """Loadable apps, through the real loader.

    The examples are assembled by mkapp.py against api/zxdesk.inc and
    nothing else, written to a device through the storage calls, and
    opened by name. The tape path is not here: it needs the ROM loader
    and a tape signal.
    """
    import base64
    import mkapi
    import mkapp
    failures = []
    print("loadable apps:")

    want = mkapi.render(mkapi.symbols("build/zxdesk.sym"))
    if open("api/zxdesk.inc").read() != want:
        failures.append("api/zxdesk.inc is stale, run ./mkapi.py")
    else:
        print("  api/zxdesk.inc matches this build")

    apps = {}
    for name in ("counter", "stopwatch", "tally"):
        apps[name] = mkapp.build(f"examples/{name}/{name}.asm")
        d = apps[name]
        print(f"  {name}: {len(d)} bytes, image "
              f"{int.from_bytes(d[4:6], 'little')}, "
              f"{int.from_bytes(d[6:8], 'little')} relocations")
    data = apps["counter"]
    n = int.from_bytes(data[4:6], "little")
    if len(data) > 256 or len(apps["stopwatch"]) > 256:
        failures.append("an example no longer fits a RAM device file")
        return failures

    BUF, NAME = 0x5B00, 0x5D10

    def put(m, name, blob):
        for i, ch in enumerate(name):
            m.poke(NAME + i, ord(ch))
        m.poke(NAME + len(name), 0)
        for i, x in enumerate(blob):
            m.poke(BUF + i, x)
        m.m.hl, m.m.b = NAME, m.sym("FA_OVERWRITE")
        m.call("StOpen")
        h = m.m.a
        m.m.a, m.m.hl, m.m.bc = h, BUF, len(blob)
        m.call("StWrite")
        m.m.a = h
        m.call("StClose")

    def run(m, name):
        for i, ch in enumerate(name):
            m.poke(NAME + i, ord(ch))
        m.poke(NAME + len(name), 0)
        m.m.hl = NAME
        m.call("ApiOpenFile")
        return m.m.a, m.m.f & 1

    def shown(m, col=3, count=3):
        x = m.peek(m.sym("WinX")) + 1 + col
        y = m.peek(m.sym("WinY")) + m.sym("WinCapH") + 4
        return "".join(next((ch for ch in "0123456789:"
                             if m.font_glyph(ch) == m.glyph_at(x + i, y)), "?")
                       for i in range(count))

    def key(m, k):
        m.m.b = m.sym(k) if isinstance(k, str) and len(k) > 1 else ord(k)
        m.call("HdlKey")

    def heap_used(m):
        at, used = m.sym("HeapBase"), 0
        while at < m.sym("HEAPEND"):
            size = m.peek16(at)
            if m.peek(at + 2):
                used += size
            at += size + m.sym("HEAPHDR")
        return used

    def loaded(m):
        return sum(m.peek(m.sym("LdrUsed") + i)
                   for i in range(m.sym("LDRSLOTS")))

    m = boot(text="")
    idle = heap_used(m)
    put(m, "COUNTER", data)
    before = m.peek(m.sym("WndCount"))
    a, cy = run(m, "COUNTER")
    base = m.peek16(m.sym("LdrBase"))
    ext = m.sym("AppExt")
    draw = int.from_bytes(data[8 + 6:8 + 8], "little")
    if (a, cy) != (0, 0) or m.peek(m.sym("WndCount")) != before + 1:
        failures.append("the app did not open a window")
        print(f"  A {a}, carry {cy}")
        return failures
    if (m.peek16(m.sym("WinApp")) != ext
            or m.peek16(ext + m.sym("APP_DRAW")) != base + draw):
        failures.append("the descriptor was not relocated")
    else:
        print(f"  loaded at ${base:04X} in the heap, descriptor relocated, "
              f"window open")
    if shown(m) != "000":
        failures.append(f"the counter drew {shown(m)!r}, not 000")
    for k in ("KEY_UP", "KEY_UP", "KEY_UP", "KEY_DOWN"):
        key(m, k)
    if shown(m) != "002":
        failures.append(f"after up, up, up, down it shows {shown(m)!r}")
    else:
        print("  it draws 000, and up, up, up, down makes it 002")
    key(m, "KEY_ENTER")
    if shown(m) != "000":
        failures.append("ENTER did not clear the count")

    # several at once: each load is its own copy with its own state
    key(m, "KEY_UP")
    a, cy = run(m, "COUNTER")
    second = m.peek16(m.sym("LdrBase"))
    if ((a, cy) != (0, 0) or second == base or shown(m) != "000"
            or m.peek16(m.sym("WinApp")) != ext + m.sym("APPSIZE")):
        failures.append("a second copy did not load beside the first")
    else:
        print(f"  a second copy loads beside it at ${second:04X} with its "
              f"own count")
    a, cy = run(m, "COUNTER")
    a4, cy4 = run(m, "COUNTER")
    if (a, cy) != (0, 0) or loaded(m) != 3:
        failures.append("the third copy did not load")
    elif (a4, cy4) != (1, 0) or not m.peek(m.sym("DgDepth")):
        failures.append("a fourth load was not refused")
    else:
        print("  three run at once, and a fourth is refused with an alert")
    m.m.a = 13
    m.call("PnlKey")
    if m.peek(m.sym("DgDepth")):
        failures.append("the alert did not close")

    # closing them all and loading again gives the memory back first
    for _ in range(3):
        m.call("WndClose")
    a, cy = run(m, "COUNTER")
    if ((a, cy) != (0, 0) or loaded(m) != 1 or shown(m) != "000"
            or m.peek(m.sym("WndCount")) != before + 1):
        failures.append("closed apps were not given back on the next load")
        print(f"  A {a}, carry {cy}, {loaded(m)} loaded")
    else:
        print("  closed apps are freed by the next load")
    m.call("WndClose")

    # files that must be refused, each leaving the heap as it was
    newer = bytearray(data)
    newer[3] = m.sym("API_VER") + 1
    stray = bytearray(data)
    stray[-2:] = (n - 1).to_bytes(2, "little")
    short = bytes(data[:-3])
    for label, blob in (("a newer interface", newer),
                        ("a relocation outside the image", stray),
                        ("a file cut short", short)):
        m2 = boot(text="")
        put(m2, "BAD", bytes(blob))
        count = m2.peek(m2.sym("WndCount"))
        a, cy = run(m2, "BAD")
        if ((a, cy) != (1, 0) or m2.peek(m2.sym("WndCount")) != count
                or heap_used(m2) != idle or loaded(m2)):
            failures.append(f"{label} was not refused cleanly")
            print(f"  {label}: A {a}, carry {cy}, heap {heap_used(m2)} "
                  f"against {idle}")
        else:
            print(f"  {label} is refused with an alert and the heap is "
                  f"as it was")

    # the counter.zxa released with 1.1.0: version 3, 22 byte descriptor
    old = base64.b64decode(COUNTER_V3)
    mo = boot(text="")
    put(mo, "OLD", old)
    a, cy = run(mo, "OLD")
    key(mo, "KEY_UP")
    mo.poke(mo.sym("TickSecs"), 3)
    mo.call("AppTick")
    tick = mo.peek16(mo.sym("AppExt") + mo.sym("APP_TICK"))
    if (a, cy) != (0, 0) or shown(mo) != "001" or tick:
        failures.append("the 1.1.0 counter file no longer loads and runs")
        print(f"  A {a}, carry {cy}, shows {shown(mo)!r}, tick ${tick:04X}")
    else:
        print("  the counter.zxa released with 1.1.0 still loads, with no "
              "tick vector")

    # a file that is not an app still opens in a notepad
    m3 = boot(text="HELLO")
    m3.call("NoteSave")
    count = m3.peek(m3.sym("WndCount"))
    a, cy = run(m3, "NOTE")
    if ((a, cy) != (0, 0) or m3.peek(m3.sym("WndCount")) != count + 1
            or m3.peek16(m3.sym("WinApp")) != m3.sym("AppNote")):
        failures.append("a document no longer opens in a notepad")
    else:
        print("  a document still opens in a notepad")

    # the stopwatch: the tick, and when it is held back
    ms = boot(text="")
    put(ms, "WATCH", apps["stopwatch"])
    a, cy = run(ms, "WATCH")

    def tick(secs):
        ms.poke(ms.sym("TickSecs"), secs)
        ms.call("AppTick")
        return shown(ms, 2, 5)

    if (a, cy) != (0, 0) or shown(ms, 2, 5) != "00:00":
        failures.append("the stopwatch did not load")
    elif tick(5) != "00:00":
        failures.append("the stopwatch ran before it was started")
    else:
        key(ms, "KEY_ENTER")
        t1, t2 = tick(5), tick(70)
        ms.poke(ms.sym("MenuOpen"), 1)
        t3 = tick(9)
        held = ms.peek(ms.sym("TickSecs"))
        ms.poke(ms.sym("MenuOpen"), 0)
        ms.call("AppTick")
        t4 = shown(ms, 2, 5)
        key(ms, "KEY_ENTER")
        t5 = tick(30)
        key(ms, "KEY_DOWN")
        t6 = shown(ms, 2, 5)
        got = (t1, t2, t3, held, t4, t5, t6)
        if got != ("00:05", "01:15", "01:15", 9, "01:24", "01:24", "00:00"):
            failures.append("the stopwatch does not keep time by the tick")
            print(f"  {got}")
        else:
            print("  the stopwatch counts by the tick: 00:05, 01:15, held "
                  "under a menu, then 01:24, stops, clears")
    # one real second through the clock
    ms.poke(ms.sym("TickSecs"), 0)
    ms.call("ClkSecond")
    if ms.peek(ms.sym("TickSecs")) != 1:
        failures.append("the clock's second does not reach the tick")
    else:
        print("  a second on the clock is a second on the tick")

    # a 128K: read out of a bank, run in the heap. The tally is too
    # big for a RAM device file, so this is also its test.
    mb = Machine("build/zxdesk.bin", "build/zxdesk.sym", banked=True)
    for rt in ("DetectMachine", "SetupIM2", "BuildScrTab", "MouseInit",
               "JoyInit", "StInit", "SetLoad", "SetApply", "DskInit",
               "InitScreen", "WndInit", "WinDraw", "WinGrab", "PtrSaveBg",
               "PtrDraw"):
        mb.call(rt)
    put(mb, "TALLY", apps["tally"])
    a, cy = run(mb, "TALLY")
    if (mb.peek(mb.sym("StBackend")) != mb.sym("ST_BANK") or (a, cy) != (0, 0)
            or shown(mb) != "000"):
        failures.append("the tally does not load from a 128K bank")
        print(f"  128K: A {a}, carry {cy}, shows {shown(mb)!r}")
    else:
        print("  on a 128K the tally loads from the bank device")
        key(mb, "L")
        none = mb.peek(mb.sym("DgDepth"))
        mb.m.a = 13
        mb.call("PnlKey")
        for k in ("KEY_UP", "KEY_UP", "KEY_UP", "S", "KEY_UP", "KEY_UP"):
            key(mb, k)
        five = shown(mb)
        key(mb, "KEY_DOWN")                 # asks; NO is the default
        asked = mb.peek(mb.sym("DgDepth"))
        mb.m.a = 13
        mb.call("PnlKey")
        kept = shown(mb)
        key(mb, "KEY_DOWN")
        for k in (mb.sym("KEY_DOWN"), 13):  # down to YES, then ENTER
            mb.m.a = k
            mb.call("PnlKey")
        cleared = shown(mb)
        key(mb, "L")
        back = shown(mb)
        got = (none, five, asked, kept, cleared, back,
               mb.peek(mb.sym("DgDepth")))
        if got != (1, "005", 1, "005", "000", "003", 0):
            failures.append("the tally's storage or dialog is wrong")
            print(f"  {got}")
        else:
            print("  L with no file alerts; S saves 003; DOWN asks, NO keeps "
                  "005, YES clears; L brings 003 back")

    # FILE, LOAD
    m4 = boot(text="")
    put(m4, "COUNTER", data)
    m4.call("FmLoadPanel")
    for k in list("COUNTER") + [4, 4, 13]:
        m4.m.a = k if isinstance(k, int) else ord(k)
        m4.call("PnlKey")
    if (m4.peek16(m4.sym("WinApp")) != m4.sym("AppExt") or m4.peek(m4.sym("FmUp"))
            or shown(m4) != "000"):
        failures.append("FILE, LOAD did not run the app")
    else:
        print("  FILE, LOAD with the name typed runs it")
    m4.call("FmLoadPanel")
    for k in list("NOPE") + [4, 4, 13]:
        m4.m.a = k if isinstance(k, int) else ord(k)
        m4.call("PnlKey")
    blank = bytes(8)
    row2 = [m4.glyph_at(m4.sym("DGX") + c, m4.sym("DGROW0") + 8)
            for c in range(1, m4.sym("DGW") - 1)]
    if not m4.peek(m4.sym("DgDepth")):
        failures.append("FILE, LOAD of a missing name said nothing")
    elif any(g != blank for g in row2):
        failures.append("an alert with one line prints rubbish on the second")
    else:
        print("  and a name that is not there gets COULD NOT LOAD, with "
              "the second line left empty")
    return failures


# examples/counter as released with 1.1.0, interface version 3.
COUNTER_V3 = (
    "WlhBA4sAEAABAHYAFgAbAEkAewAKPAwkAAAAAAAArzJ2AMk6dgAhdwAOZM1AAA4KzUAAxjB3"
    "IXcAAQMEzVeAIYMAAQEOw1eANi80kTD8gSPJ/gMoCv4EKAz+DcCvGAo6dgA8GAQ6dgA9MnYA"
    "zSSAzV2AzRsAzRWAzSeAwyqAADAwMABDT1VOVEVSAFVQIERPV04AAgAEAAYACAAKABgAHAAf"
    "ACQAKQAvADgAWABeAGIAawA=")


def watchdog_checks():
    """A3. Deliberately overrun a frame and see it reported.

    The handler counts interrupts and FrameWatch counts frames the main
    loop reached the top of, so the two diverge exactly when a frame's
    work ran past the interrupt. The test drives that directly: take
    some interrupts without letting the loop see them, then call
    FrameWatch and read the count.

    The handler is checked separately, and the check that matters is
    not that it counts but that it puts back everything it borrowed. It
    fires inside a fill, where the stack is not a stack and AF' is
    carrying DevFillDesk's lattice pattern, so a handler that pushed or
    used EXX would corrupt the picture rather than the counter.
    """
    failures = []
    m = boot(text="")
    m.m.set_get_int_vector_callback(lambda: 0xFF)

    def dropped():
        return m.peek16(m.sym("Dropped"))

    # a frame in step: one interrupt arrived and the loop woke on it
    m.poke16(m.sym("IrqCnt"), m.peek(m.sym("FrameCnt")) + 1)
    m.call("FrameWatch")
    if dropped() != 0:
        failures.append("watchdog reports a drop on a clean frame")
        print(f"  a frame in step already reads {dropped()} dropped")
    else:
        print("watchdog: a frame in step reports nothing")

    # three interrupts the loop never got to see
    for n, want in ((3, 2), (1, 2), (5, 6)):
        before = dropped()
        m.poke16(m.sym("IrqCnt"), m.peek(m.sym("FrameCnt")) + n)
        m.call("FrameWatch")
        got = dropped() - before
        print(f"  {n} interrupts missed, reported {got} frames dropped")
        if got != n - 1:
            failures.append(f"watchdog miscounts {n} missed interrupts")
            print(f"    expected {n - 1}: the loop sees one of them itself")

    # the handler must put back every register it borrows
    regs = dict(af=0x1234, bc=0x2345, de=0x3456, hl=0x4567,
                ix=0x5678, iy=0x6789, alt_bc=0x789A, alt_de=0x89AB,
                alt_hl=0x9ABC)
    before_cnt = m.peek16(m.sym("IrqCnt"))
    m.poke(0x0100, 0x76)
    m.m.sp = STACK - 2
    m.poke16(m.m.sp, 0x0100)
    for k, v in regs.items():
        setattr(m.m, k, v)
    m.m.pc = m.sym("IrqTick")
    sp_before = m.m.sp
    while not m.m.halted:
        m.m.ticks_to_stop = 1
        m.m.run()
    changed = [k for k, v in regs.items() if getattr(m.m, k) != v]
    if m.peek16(m.sym("IrqCnt")) != (before_cnt + 1) & 0xFFFF:
        failures.append("the handler does not count")
        print("  the handler did not increment IrqCnt")
    if changed:
        failures.append("the handler clobbers registers")
        print(f"  the handler did not put back: {', '.join(changed)}")
    elif m.m.sp != (sp_before + 2) & 0xFFFF:
        # entered directly rather than through an interrupt, so the RET
        # pops the sentinel. Two bytes and not one more is the point:
        # a handler that pushed anything would show up here.
        failures.append("the handler uses more stack than its return address")
        print(f"  the handler left SP at {m.m.sp:04X}, "
              f"not {(sp_before + 2) & 0xFFFF:04X}")
    else:
        print("  the handler counts and puts back AF, BC, DE, HL, IX, IY "
              "and the alternate set")
    return failures


# A2. The rectangle the interrupt sweep fills, and the trampoline it is
# called through. $7F00 is free RAM below the code.
SWEEP = (8, 64, 8, 24)          # FrX, FrY, FrW, FrH
TRAMP = 0x7F00


def interrupt_checks():
    """Sweep an interrupt across every instruction of a fill.

    This is A2's acceptance test and it is the whole reason A2 was
    rewritten. The fills used to hold DI for their entire run, which
    does not delay the interrupt: the Spectrum asserts INT for 32 T
    and then withdraws it, so an interrupt refused inside that window
    never happens at all. Measured here before the rewrite, an
    injected interrupt was refused at 458 of 500 instruction
    boundaries in DevFillRect and 710 of 752 in DevFillDesk. The 42
    that survived in each were the setup before the DI.

    What makes the sweep worth having rather than an argument on
    paper is the second and third columns. SP walks through screen
    memory during a fill, so accepting an interrupt means the CPU
    pushes two bytes of return address into the display. The fill is
    built so those two bytes always land where they are about to be
    overwritten, and the only way to believe that is to inject an
    interrupt at every boundary and compare the pixels, inside the
    rectangle and outside it.
    """
    x, y, w, h = SWEEP

    def run(routine, inject_after=None):
        m = boot(text="")
        m.m.set_get_int_vector_callback(lambda: 0xFF)
        for name, val in (("FrX", x), ("FrY", y), ("FrW", w), ("FrH", h)):
            m.poke(m.sym(name), val)
        m.poke16(m.sym("FrPat"), 0xFFFF)
        addr = m.sym(routine)
        m.m.set_memory_block(TRAMP, bytes([0xFB, 0xCD, addr & 0xFF,
                                           addr >> 8, 0x76]))
        m.m.sp = STACK
        m.m.pc = TRAMP
        steps = 0
        while steps < 400_000:
            m.m.ticks_to_stop = 1               # one instruction at a time
            m.m.run()
            if m.m.halted:
                return m, steps
            steps += 1
            if steps == inject_after:
                if not m.m.on_handle_active_int():
                    return None, steps          # refused, so destroyed
        raise RuntimeError(f"{routine} never returned")

    def rect(m):
        return bytes(m.peek(m.scr_addr(c, r))
                     for r in range(y, y + h) for c in range(x, x + w))

    def rest(m):
        return bytes(m.peek(m.scr_addr(c, r))
                     for r in range(192) for c in range(32)
                     if not (y <= r < y + h and x <= c < x + w))

    failures = []
    print(f"interrupt sweep over a {w} by {h} fill:")
    for routine in ("DevFillRect", "DevFillDesk"):
        base, total = run(routine)
        clean, clean_rest = rect(base), rest(base)
        lost = wrong = spill = 0
        for n in range(2, total + 1):           # 1 is the trampoline's own EI
            m, _ = run(routine, inject_after=n)
            if m is None:
                lost += 1
                continue
            if rect(m) != clean:
                wrong += 1
            if rest(m) != clean_rest:
                spill += 1
        print(f"  {routine}: {total} instructions, an interrupt injected "
              f"after each")
        print(f"    destroyed {lost}, rectangle wrong {wrong}, "
              f"pixels outside it {spill}")
        if lost or wrong or spill:
            failures.append(f"{routine} interrupt sweep")
    return failures


def settings_checks():
    """The settings panel, and whether a toggle reaches the machine.

    Since D2 the panel is a table rather than code, so these checks
    are about two things at once: that the control library paints and
    dispatches what the table says, and that a settings change still
    reaches the running system.

    The pixel half is what the bench subject cannot see. That every
    row prints its label and its value without running into each
    other, which INVERT Y did at column nine. That the focus bar is on
    exactly one row and moves when the keyboard says so. That
    switching the lattice off changes the desktop rather than only the
    value. And that switching it back on restores the desktop byte for
    byte, which is the check that catches a stale save under: the
    panel's buffer is grabbed before the repaint and put back after
    it, so getting the order wrong leaves a rectangle of the old
    background behind.
    """
    failures = []
    m = boot()
    # park the pointer off the panel, or its image lands in the rows
    # being read back and every glyph under it is a mismatch
    m.poke(m.sym("PtrX"), 230)
    m.poke(m.sym("PtrY"), 150)
    m.call("PtrSaveBg")
    m.call("PtrDraw")
    m.call("DlgOpen")
    X, R0 = m.sym("DLGX"), m.sym("DLGROW0")
    ROWS = m.peek(m.sym("PnlRows"))

    width = m.peek(m.sym("PnlW"))

    def read_line(line):
        out = []
        for c in range(1, width - 1):
            got = m.glyph_at(X + c, R0 + line * 8)
            out.append(next((ch for ch in " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
                             if m.font_glyph(ch) == got), "?"))
        return "".join(out).rstrip()

    def barred():
        """Which rows are carrying the focus bar, read off the screen."""
        mask = m.sym("PNLMARGIN")
        out = []
        for line in range(ROWS):
            px = [m.peek(m.scr_addr(X, R0 + line * 8 + i)) & mask
                  for i in range(8)]
            if all(p == mask for p in px):
                out.append(line)
            elif any(px):
                out.append(f"{line} partial")
        return out

    print("settings panel, read back off the screen:")
    want = ["SPEED      MED", "INVERT Y    ON", "LATTICE     ON",
            "KEY PTR    EXT", "SOUND       ON", "SAVE", "DONE"]
    for i, expect in enumerate(want):
        got = read_line(i)
        print(f"  row {i}: |{got}|")
        if got != expect:
            failures.append(f"panel row {i}")
            print(f"    expected |{expect}|")

    # D2's signature element: the focus bar, on exactly one row, moving
    # under the keyboard so a panel can be driven with no mouse at all
    if barred() != [0]:
        failures.append("the focus bar does not start on row 0")
        print(f"  the bar is on {barred()}, expected row 0 alone")
    else:
        print("  the focus bar starts on row 0")
    for _ in range(3):
        m.m.a = 4                                   # KEY_DOWN
        m.call("DlgKey")
    if m.peek(m.sym("PnlFocus")) != 3 or barred() != [3]:
        failures.append("the focus bar does not follow the keyboard")
        print(f"  after three downs the bar is on {barred()}, "
              f"PnlFocus is {m.peek(m.sym('PnlFocus'))}")
    else:
        print("  three downs move it to row 3, and only row 3 carries it")
    for _ in range(9):
        m.m.a = 4
        m.call("DlgKey")
    if m.peek(m.sym("PnlFocus")) != ROWS - 1:
        failures.append("the focus bar runs off the bottom")
        print(f"  nine more downs took it to {m.peek(m.sym('PnlFocus'))}, "
              f"not {ROWS - 1}")
    else:
        print(f"  it stops at the last row rather than running off")
    for _ in range(20):
        m.m.a = 3                                   # KEY_UP
        m.call("DlgKey")
    if m.peek(m.sym("PnlFocus")) != 0:
        failures.append("the focus bar runs off the top")

    # ENTER on a cycle row must do exactly what a press on it does
    m.m.a = 4
    m.call("DlgKey")                                # down to INV Y
    before = m.peek(m.sym("SetInvertY"))
    m.m.a = 13                                      # KEY_ENTER
    m.call("DlgKey")
    after = m.peek(m.sym("SetInvertY"))
    if after == before:
        failures.append("ENTER does not activate the focused row")
        print(f"  ENTER left INV Y at {before}")
    else:
        print(f"  ENTER on INV Y cycles it, {before} to {after}, and the row "
              f"reads |{read_line(1)}|")
    m.m.a = 13
    m.call("DlgKey")                                # and back, so the rest
    m.m.a = 3                                       # of the checks start clean
    m.call("DlgKey")

    # a strip of desktop well clear of both the panel and the window
    def desktop_strip():
        return bytes(m.peek(m.scr_addr(c, r))
                     for r in range(150, 170) for c in range(1, 8))

    def click(line):
        m.m.c = R0 + line * 8
        m.call("DlgClick")

    dots = desktop_strip()
    click(2)                                    # LATTICE
    plain = desktop_strip()
    if m.peek(m.sym("SetLattice")) != 0:
        failures.append("lattice toggle did not store")
    if plain == dots:
        failures.append("lattice off did not change the desktop")
        print("  switching the lattice off left the desktop unchanged")
    elif any(plain):
        failures.append("lattice off is not plain")
        print(f"  the plain desktop still has ink: {plain[:8].hex()}")
    else:
        print("  lattice off repaints the desktop plain")

    click(2)
    if desktop_strip() != dots:
        failures.append("lattice on did not restore the desktop")
        print("  switching the lattice back on did not restore the dots")
    else:
        print("  lattice on restores the dot lattice byte for byte")

    # and the panel is still intact on top of the two repaints
    if read_line(2) != "LATTICE     ON":
        failures.append("panel damaged by the lattice repaint")
        print(f"  the panel now reads |{read_line(2)}|")
    else:
        print("  the panel survives the repaint underneath it")
    return failures


def pointer_key_checks(m):
    """The pointer keys must not fire on an unshifted key any more.

    Typing O used to move the pointer left, because the cursor keys and
    QAOP drove it back when the keyboard was a pointer device and
    nothing else. This is the check that says they no longer do.
    """
    failures = []
    CAPSROW, SYMROW, DIGITS = 0xFE, 0x7F, 0xF7

    def press(rows):
        m.keys = dict(rows)
        m.poke(m.sym("PtrX"), 120)
        for _ in range(4):
            m.call("ReadInput")
        return m.peek(m.sym("PtrX"))

    plain = press({DIGITS: 0xFF & ~0x10})           # 5 on its own
    if plain != 120:
        failures.append("unshifted 5 still moves the pointer")
        print(f"  unshifted 5 moved the pointer to {plain}, expected 120")
    else:
        print("  unshifted 5 types rather than moving the pointer")

    shifted = press({DIGITS: 0xFF & ~0x10,
                     CAPSROW: 0xFF & ~0x01,
                     SYMROW: 0xFF & ~0x02})         # EXTEND MODE and 5
    if shifted >= 120:
        failures.append("EXTEND MODE and 5 does not move the pointer")
        print(f"  EXTEND MODE and 5 left the pointer at {shifted}")
    else:
        print(f"  EXTEND MODE and 5 moves the pointer left, 120 to {shifted}")
    m.keys = {}
    return failures


sys.exit(main())
