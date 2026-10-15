
; ------------------------------------------------------------
;  WinEraseDamage
;  A full erase repaints 72 rows of desktop and BlitRect then
;  covers almost all of it again. Only the part of the old
;  rectangle the new one does not cover actually needs painting,
;  which for a drag of a few pixels is a thin strip.
;
;  Up to two strips: one across the top or bottom for vertical
;  movement, one down the left or right edge for horizontal. If
;  the rectangles do not overlap at all, fall through to erasing
;  the lot, which is also what happens on the first frame of a
;  drag when the window jumps.
;
;  A strip may extend into the new window position. That is
;  harmless: BlitRect paints over it immediately afterwards.
; ------------------------------------------------------------
WinEraseDamage:
                ld      a,(WinOldY)
                ld      b,a
                ld      a,(WinY)
                sub     b
                ld      (WeDy),a
                ld      a,(WinOldX)
                ld      b,a
                ld      a,(WinX)
                sub     b
                ld      (WeDx),a

                ld      a,(WeDy)        ; any rows in common?
                call    WeAbs
                ld      b,a
                ld      a,(WinH)
                cp      b
                jp      c,WinErase
                jp      z,WinErase
                ld      a,(WeDx)        ; any columns in common?
                call    WeAbs
                ld      b,a
                ld      a,(WinW)
                cp      b
                jp      c,WinErase
                jp      z,WinErase

                ; strip across the top or the bottom
                ld      a,(WeDy)
                or      a
                jr      z,WeNoVert
                ld      a,(WinOldX)
                ld      (FrX),a
                ld      a,(WinW)
                ld      (FrW),a
                ld      a,(WeDy)
                bit     7,a
                jr      nz,WeVertUp
                ld      (FrH),a         ; moved down: vacated the old top
                ld      a,(WinOldY)
                ld      (FrY),a
                jr      WeVertGo
WeVertUp:
                neg                     ; moved up: vacated the old bottom
                ld      (FrH),a
                ld      c,a
                ld      a,(WinOldY)
                ld      b,a
                ld      a,(WinH)
                add     a,b
                sub     c
                ld      (FrY),a
WeVertGo:
                call    WeFillStrip
WeNoVert:
                ; strip down the left or right edge, over the rows the
                ; two rectangles have in common
                ld      a,(WeDx)
                or      a
                ret     z
                bit     7,a
                jr      nz,WeHorzLeft
                ld      (FrW),a         ; moved right: vacated the old left
                ld      a,(WinOldX)
                ld      (FrX),a
                jr      WeHorzRows
WeHorzLeft:
                neg                     ; moved left: vacated the old right
                ld      (FrW),a
                ld      c,a
                ld      a,(WinOldX)
                ld      b,a
                ld      a,(WinW)
                add     a,b
                sub     c
                ld      (FrX),a
WeHorzRows:
                ld      a,(WinOldY)     ; top of the shared rows
                ld      b,a
                ld      a,(WinY)
                cp      b
                jr      nc,WeRowsSet
                ld      a,b
WeRowsSet:
                ld      (FrY),a
                ld      a,(WeDy)
                call    WeAbs
                ld      b,a
                ld      a,(WinH)
                sub     b
                ret     z
                ld      (FrH),a
                ; DevFillDesk works in pairs of bytes, so a one column
                ; move would give a width of 1, be rounded down to
                ; nothing, and leave a column of residue behind. Widen
                ; it instead, away from the right hand edge.
                ld      a,(FrW)
                bit     0,a
                jr      z,WeEven
                inc     a
                ld      (FrW),a
                ld      b,a
                ld      a,(FrX)
                add     a,b
                cp      SCRCOLS+1
                jr      c,WeEven
                ld      a,(FrX)
                dec     a
                ld      (FrX),a
WeEven:
                jp      WeFillStrip

; ------------------------------------------------------------
;  Narrow desktop fill, for a column strip two bytes across
;  DevFillDesk builds a push chain and works the lattice pattern
;  out afresh on every row. Over a window sixteen bytes wide that
;  is the right trade and the pushes dominate. Over a column it is
;  the wrong one and the setup dominates: a one column drag was
;  paying 14,256 T to write 144 bytes, 198 T a row, 99 T a byte.
;
;  Here the two bytes go straight through HL and the pattern is
;  held in registers, unrolled four ways so no row works out which
;  of the three lattice bytes it needs. The cycle is $88, blank,
;  $22, blank, so the entry point comes from FrY and 3.
;
;  FrW must be exactly 2. WeEven guarantees it for a single column
;  move; anything wider still belongs in the push chain.
; ------------------------------------------------------------
DeskFillCol:
DeskHook2:      ld      a,(FrH)         ; SetApply makes this JP WallFill
                or      a               ; while the desktop is a picture
                ret     z
                ld      a,(FrX)
                ld      c,a
                ld      a,(FrY)
                call    AddrAt
                ld      a,(FrH)
                ld      b,a
                ld      c,0             ; blank rows
                ld      d,$88
LatA2           equ     $-1             ; SetApply patches both operands
                ld      e,$22
LatB2           equ     $-1
                ld      a,(FrY)
                and     3
                jr      z,DfcP0
                dec     a
                jr      z,DfcP1
                dec     a
                jr      z,DfcP2
                jr      DfcP3

DfcP0:          ld      (hl),d
                inc     l
                ld      (hl),d
                dec     l
                inc     h
                ld      a,h
                and     7
                jr      nz,DfcS0
                ld      a,l
                add     a,SCRCOLS
                ld      l,a
                jr      c,DfcS0
                ld      a,h
                sub     8
                ld      h,a
DfcS0:          djnz    DfcP1
                ret
DfcP1:          ld      (hl),c
                inc     l
                ld      (hl),c
                dec     l
                inc     h
                ld      a,h
                and     7
                jr      nz,DfcS1
                ld      a,l
                add     a,SCRCOLS
                ld      l,a
                jr      c,DfcS1
                ld      a,h
                sub     8
                ld      h,a
DfcS1:          djnz    DfcP2
                ret
DfcP2:          ld      (hl),e
                inc     l
                ld      (hl),e
                dec     l
                inc     h
                ld      a,h
                and     7
                jr      nz,DfcS2
                ld      a,l
                add     a,SCRCOLS
                ld      l,a
                jr      c,DfcS2
                ld      a,h
                sub     8
                ld      h,a
DfcS2:          djnz    DfcP3
                ret
DfcP3:          ld      (hl),c
                inc     l
                ld      (hl),c
                dec     l
                inc     h
                ld      a,h
                and     7
                jr      nz,DfcS3
                ld      a,l
                add     a,SCRCOLS
                ld      l,a
                jr      c,DfcS3
                ld      a,h
                sub     8
                ld      h,a
DfcS3:          djnz    DfcP0
                ret

; A = value, returns |A|
WeAbs:
                or      a
                ret     p
                neg
                ret

WeDx:           defb    0
WeDy:           defb    0
