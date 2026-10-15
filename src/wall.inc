; ============================================================
;  Wallpaper
;  A picture behind the windows instead of the lattice. The
;  picture is a band of sixteen character rows, pixel rows 32 to
;  159, kept linear at WALLADDR in bank 1: thirty two bytes a
;  pixel row, top to bottom, so a rectangle of it is a run of
;  bytes per row and a step of thirty two between rows.
;  Everything above and below the band is plain.
;
;  It is a 128K enhancement, gated on Is128 like the RAM disk. A
;  48K has nowhere to keep 4,096 bytes: the heap could spare them
;  only by making a tiled window's buffer, 5,568 bytes for the
;  whole desktop, impossible. Bank 1 holds a 128 byte directory
;  and nothing else, so the band goes above it. The tape's
;  second block lands in the screen and BankWallBoot moves it.
;
;  The lattice is painted by three routines with the pattern
;  patched in as immediates. The picture cannot be an immediate,
;  so this is a fourth routine, and SetApply makes DevFillDesk
;  and DeskFillCol jump here by patching their first instruction
;  while the setting says IMAGE. The erase path pays nothing for
;  the choice when the lattice is showing.
;
;  Costs, derived, not measured: a band row two bytes wide is
;  about 106 T including the step, so a single column drag of 96
;  rows is about 10,200 T against DeskFillCol's measured 5,712.
;  A row W bytes wide through LDIR is about 150 + 21W T, so a
;  full 16 by 96 erase is about 46,700 T against DevFillDesk's
;  30,125. Both are inside the drag frame's 18,384 T of headroom
;  for the strips a drag actually vacates.
; ============================================================
; WALLBANK, WALLADDR and WALLSIZE are with the memory map in zxdesk.asm.
WALLTOP         equ     32              ; first pixel row the band covers
WALLROWS        equ     128
WALLBOT         equ     WALLTOP+WALLROWS

; in: FrX, FrY, FrW, FrH, as DevFillDesk. Any width.
WallFill:
                ld      hl,(FrW)                ; L = width, H = rows
                ld      a,h
                or      a
                ret     z
                ld      (WfLeft),a
                ld      a,l
                or      a
                ret     z
                ld      (WfWidth),a
                ld      hl,(FrX)                ; L = column, H = row
                ld      c,l
                ld      a,h
                ld      (WfRow),a
                call    AddrAt                  ; HL = first row on the screen

                ; rows above the band are plain
                ld      a,(WfRow)
                cp      WALLTOP
                jr      nc,WfBand
                ld      c,a
                ld      a,WALLTOP
                sub     c                       ; rows to the band
                call    WfTake                  ; B = min(rows, left)
                call    WfPlain
                ld      a,(WfLeft)
                or      a
                ret     z
WfBand:
                ld      a,(WfRow)
                cp      WALLBOT
                jr      nc,WfBelow
                ld      c,a
                ld      a,WALLBOT
                sub     c                       ; rows to the bottom of the band
                call    WfTake                  ; moves WfRow on, so the
                push    bc                      ; first band row is C
                ; source = WALLADDR + (row - WALLTOP) * 32 + X
                ld      a,c
                sub     WALLTOP
                ld      e,a
                ld      d,0
                ex      de,hl
                add     hl,hl
                add     hl,hl
                add     hl,hl
                add     hl,hl
                add     hl,hl
                ld      a,(FrX)
                ld      c,a
                ld      b,0
                add     hl,bc
                ld      bc,WALLADDR
                add     hl,bc
                ex      de,hl                   ; DE = source, HL = screen
                ; the band is in bank 1. The screen, the stack and every
                ; Fr* are below $C000, and the interrupt handler touches
                ; nothing above it, so paging here needs no DI. PageCur
                ; keeps the other bits of the port, which is write only.
                ld      a,(PageCur)
                or      WALLBANK
                ld      bc,PAGEPORT
                out     (c),a
                pop     bc                      ; B = band rows
                ld      a,(WfWidth)
                cp      2
                jr      z,WfNarrow
WfWide:
                push    bc
                push    hl
                push    de
                ex      de,hl                   ; LDIR wants the source in HL
                ld      bc,(WfWidth)            ; the high byte is always nought
                ldir
                pop     de
                pop     hl
                pop     bc
                call    ScrDown
                ld      a,e
                add     a,SCRCOLS
                ld      e,a
                jr      nc,WfWideNext
                inc     d
WfWideNext:
                djnz    WfWide
                jr      WfBandDone
; Two bytes a row, the width every single column drag has, straight
; through the registers. Neither inc crosses a page: the screen byte
; is at most column 30 and the band row is 32 aligned.
WfNarrow:
                ld      a,(de)
                ld      (hl),a
                inc     e
                inc     l
                ld      a,(de)
                ld      (hl),a
                dec     l
                ld      a,e
                add     a,SCRCOLS-1
                ld      e,a
                jr      nc,WfNarrowNext
                inc     d
WfNarrowNext:
                call    ScrDown
                djnz    WfNarrow
WfBandDone:
                ld      a,(PageCur)             ; bank 0 back; B is spent
                ld      bc,PAGEPORT
                out     (c),a
                ld      a,(WfLeft)
                or      a
                ret     z
WfBelow:
                ; rows below the band are plain, and the last thing painted
                ld      a,(WfLeft)
                ld      b,a
                jp      WfPlain

; A = rows to the next boundary. B = the smaller of that and the
; rows left; the bookkeeping moves on by B.
WfTake:
                ld      b,a
                ld      a,(WfLeft)
                cp      b
                jr      nc,WfTakeAll
                ld      b,a
WfTakeAll:
                ld      a,(WfLeft)
                sub     b
                ld      (WfLeft),a
                ld      a,(WfRow)
                add     a,b
                ld      (WfRow),a
                ret

; B rows of WfWidth bytes from HL, blank. HL is left on the row after.
WfPlain:
                push    bc
                push    hl
                ld      a,(WfWidth)
                ld      b,a
                xor     a
WfPlainByte:
                ld      (hl),a
                inc     l
                djnz    WfPlainByte
                pop     hl
                call    ScrDown
                pop     bc
                djnz    WfPlain
                ret

WfWidth:        defw    0               ; low byte the width, high byte nought
WfRow:          defb    0
WfLeft:         defb    0
