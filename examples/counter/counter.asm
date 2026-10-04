; Counter: the smallest useful ZX Desk app.
; UP and DOWN change the number, ENTER clears it.
;
;   ./mkapp.py examples/counter/counter.asm build/counter.zxa

                include "zxdesk.inc"

                org     APPORG

; The descriptor comes first. The desktop copies it at load.
                defw    1               ; bytes of state kept per window
                defw    Count           ; where that state lives while in use
                defw    Init            ; called once for a new window
                defw    Draw            ; paint the inside of the window
                defw    Key             ; A = a key
                defw    Title
                defb    10, 60          ; column, pixel row it opens at
                defb    12, 36          ; width in columns, height in rows
                defw    0, 0, 0         ; close, scroll, scroll to: unused

Init:
                xor     a
                ld      (Count),a
                ret

; B = pixel rows below the title bar, C = columns in from the border.
Draw:
                ld      a,(Count)
                ld      hl,Digits
                ld      c,100
                call    Digit
                ld      c,10
                call    Digit
                add     a,'0'
                ld      (hl),a
                ld      hl,Digits
                ld      bc,4*256+3
                call    ApiWinPrint
                ld      hl,Help
                ld      bc,14*256+1
                jp      ApiWinPrint

Digit:
                ld      (hl),'0'-1
DigitLoop:
                inc     (hl)
                sub     c
                jr      nc,DigitLoop
                add     a,c
                inc     hl
                ret

Key:
                cp      KEY_UP
                jr      z,Up
                cp      KEY_DOWN
                jr      z,Down
                cp      KEY_ENTER
                ret     nz
                xor     a
                jr      Store
Up:
                ld      a,(Count)
                inc     a
                jr      Store
Down:
                ld      a,(Count)
                dec     a
Store:
                ld      (Count),a
                ; Repainting: lift the pointer, draw, then tell the
                ; desktop the window changed and put the pointer back.
                call    ApiPtrRestore
                call    ApiWinClear
                call    Draw
                call    ApiWinGrab
                call    ApiPtrSaveBg
                jp      ApiPtrDraw

Count:          defb    0
Digits:         defb    "000",0
Title:          defb    "COUNTER",0
Help:           defb    "UP DOWN",0
