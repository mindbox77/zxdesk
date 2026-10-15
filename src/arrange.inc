; ============================================================
;  Arranging the windows
;  GEM's desktop could tidy up after you and this is the same
;  two commands. Cascade steps them down and right so every title
;  bar is readable; tile divides the desktop between them so
;  nothing overlaps at all.
;
;  The two are not the same kind of operation and it is worth
;  saying why. Cascade moves windows and does not resize them, so
;  it touches no memory and cannot fail. Tile resizes them, which
;  means every window gives its buffer back and asks for another,
;  and the heap can say no. So tile goes one window at a time
;  through the live record, exactly the way the resize grip does,
;  and puts a window's old size back if the heap refuses it.
;
;  Cascade edits the table directly instead, because it changes
;  nothing that WndAllocBuf cares about, and walking the table is
;  cheaper than selecting five windows in turn. The live record
;  is reloaded from the table afterwards, because WndRepaintAll
;  begins with a WndFlush and a stale live record would write
;  itself straight back over the arrangement.
;
;  Both are menu items, so both are in the slow region.
; ============================================================
ARRSTEPX        equ     2               ; byte columns a cascade steps
ARRSTEPY        equ     10              ; and pixel rows, which is a cap
ARRX0           equ     1
ARRY0           equ     DESKTOP_TOP+1
ARRDESKH        equ     DESKTOP_BOT-DESKTOP_TOP+1
ARRHALFH        equ     ARRDESKH/2

; ------------------------------------------------------------
;  WndCascade
;  Back to front, each one a step further down and right, so the
;  front window is the one furthest from the corner and every
;  title bar behind it is showing.
; ------------------------------------------------------------
WndCascade:
                ld      a,(WndCount)
                or      a
                ret     z                       ; ArrDone would load a closed
                                                ; window's record into the live one
                call    WndFlush
                ld      a,(WndCount)
                ld      (ArrN),a
                xor     a
                ld      (ArrI),a
ArcLoop:
                ld      a,(ArrN)
                ld      b,a
                ld      a,(ArrI)
                cp      b
                jr      nc,ArrDone
                ; the window at that depth, counting from the back
                ld      c,a
                ld      a,(ArrN)
                dec     a
                sub     c
                ld      e,a
                ld      d,0
                ld      hl,WndZ
                add     hl,de
                ld      a,(hl)
                call    WndRecAt                ; HL = its record
                push    hl
                ; x, stepped and then held off the right edge
                ld      a,(ArrI)
                ld      b,a
                add     a,a                     ; ARRSTEPX is two
                add     a,ARRX0
                ld      c,a
                pop     hl
                push    hl
                inc     hl
                inc     hl
                ld      a,(hl)                  ; its width
                neg
                add     a,SCRCOLS
                cp      c
                jr      nc,ArcXok
                ld      c,a
ArcXok:
                pop     hl
                push    hl
                ld      (hl),c                  ; WinX
                ld      de,12
                add     hl,de
                ld      (hl),c                  ; WinOldX, so the next damage
                pop     hl                      ; is not computed from where
                push    hl                      ; it used to be
                ; y, the same way against the status rule
                ld      a,(ArrI)
                ld      b,a
                add     a,a
                add     a,a
                add     a,b
                add     a,a                     ; times ten
                add     a,ARRY0
                ld      c,a
                pop     hl
                push    hl
                inc     hl
                inc     hl
                inc     hl
                ld      a,(hl)                  ; its height
                neg
                add     a,STATRULE
                cp      c
                jr      nc,ArcYok
                ld      c,a
ArcYok:
                pop     hl
                inc     hl
                ld      (hl),c                  ; WinY
                ld      de,12
                add     hl,de
                ld      (hl),c                  ; WinOldY
                ld      hl,ArrI
                inc     (hl)
                jr      ArcLoop

; The table is the truth now, so the live copy is refreshed from it
; before anything flushes the other way.
ArrDone:
                ld      a,(WndCur)
                call    WndRecAt
                ld      de,WinRec
                ld      bc,WNDRECSZ
                ldir
                xor     a
                ld      (WinMoved),a
                jp      WndRepaintAll

; ------------------------------------------------------------
;  WndTile
;  The desktop divided between them. One window gets all of it,
;  two split it left and right, three put one down the left and
;  stack two on the right, four make quarters.
;
;  Every window is selected in turn rather than edited in the
;  table, because the size changes and WndAllocBuf works on the
;  live record. That also means the state swap happens, which is
;  what keeps a notepad's document with its own window.
; ------------------------------------------------------------
WndTile:
                ld      a,(WndCount)
                or      a
                ret     z                       ; ArrTileAt has no row for nought
                call    WndFlush
                ld      a,(WndCount)
                dec     a
                ld      e,a
                ld      d,0
                ld      hl,ArrTileAt
                add     hl,de
                ld      a,(hl)                  ; where this count's rows start
                ld      e,a
                ld      hl,ArrTiles
                add     hl,de
                ld      (ArrTab),hl
                ; Every buffer goes back before any is asked for. Doing it
                ; one window at a time works until the heap is fragmented,
                ; and then the last window asks for more than any single
                ; hole holds and keeps its old size: a tile with one
                ; window the wrong shape in it. Freeing them all first
                ; coalesces the lot into one block.
                xor     a
                ld      (ArrI),a
ArtFree:
                ld      a,(WndCount)
                ld      b,a
                ld      a,(ArrI)
                cp      b
                jr      nc,ArtFreed
                ld      e,a
                ld      d,0
                ld      hl,WndZ
                add     hl,de
                ld      a,(hl)
                call    WndSelect
                ld      hl,(WinBufP)
                call    HeapFree
                ld      hl,0
                ld      (WinBufP),hl
                ld      hl,ArrI
                inc     (hl)
                jr      ArtFree
ArtFreed:
                xor     a
                ld      (ArrI),a
ArtLoop:
                ld      a,(WndCount)
                ld      b,a
                ld      a,(ArrI)
                cp      b
                jr      nc,ArtDone
                ld      e,a
                ld      d,0
                ld      hl,WndZ
                add     hl,de
                ld      a,(hl)
                call    WndSelect               ; and its document with it
                ; keep the old size, because the heap may refuse the new one
                ld      a,(WinW)
                ld      (ArrOldW),a
                ld      a,(WinH)
                ld      (ArrOldH),a
                ld      hl,(ArrTab)
                ld      a,(hl)
                ld      (WinX),a
                ld      (WinOldX),a
                inc     hl
                ld      a,(hl)
                ld      (WinY),a
                ld      (WinOldY),a
                inc     hl
                ld      a,(hl)
                ld      (WinW),a
                inc     hl
                ld      a,(hl)
                ld      (WinH),a
                inc     hl
                ld      (ArrTab),hl
                call    WndAllocBuf
                jr      nc,ArtNext
                ; The heap could not carry it. The old size goes back and
                ; that cannot fail, for the same reason it cannot in a
                ; resize: WndAllocBuf gave the old buffer up before it
                ; asked for the new one.
                ld      a,(ArrOldW)
                ld      (WinW),a
                ld      a,(ArrOldH)
                ld      (WinH),a
                call    WndAllocBuf
ArtNext:
                xor     a
                ld      (WinMoved),a
                ld      hl,ArrI
                inc     (hl)
                jr      ArtLoop
ArtDone:
                ld      a,(WndZ)                ; the front one is live again
                call    WndSelect
                jp      WndRepaintAll

; Where each count's rectangles begin, in bytes from ArrTiles.
ArrTileAt:      defb    0, 4, 12, 24

; x, y, w, h. Widths are even because DevFillRect works in pairs and
; rounds an odd one down, which is the same rule the menus follow.
ArrTiles:
                ; one window: the whole desktop
                defb    0, DESKTOP_TOP, SCRCOLS, ARRDESKH
                ; two: left and right, full height
                defb    0, DESKTOP_TOP, 16, ARRDESKH
                defb    16, DESKTOP_TOP, 16, ARRDESKH
                ; three: one down the left, two stacked on the right
                defb    0, DESKTOP_TOP, 16, ARRDESKH
                defb    16, DESKTOP_TOP, 16, ARRHALFH
                defb    16, DESKTOP_TOP+ARRHALFH, 16, ARRDESKH-ARRHALFH
                ; four: quarters
                defb    0, DESKTOP_TOP, 16, ARRHALFH
                defb    0, DESKTOP_TOP+ARRHALFH, 16, ARRDESKH-ARRHALFH
                defb    16, DESKTOP_TOP, 16, ARRHALFH
                defb    16, DESKTOP_TOP+ARRHALFH, 16, ARRDESKH-ARRHALFH

ArrN:           defb    0
ArrI:           defb    0
ArrTab:         defw    0
ArrOldW:        defb    0
ArrOldH:        defb    0
