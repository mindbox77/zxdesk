; Stopwatch: an app that uses the tick.
; ENTER starts and stops it, DOWN clears it.
;
;   ./mkapp.py examples/stopwatch/stopwatch.asm build/stopwatch.zxa

                include "zxdesk.inc"

                org     APPORG

                defw    STATE           ; bytes of state kept per window
                defw    Secs            ; where it lives while in use
                defw    Init
                defw    Draw
                defw    Key
                defw    Title
                defb    12, 104         ; column, pixel row it opens at
                defb    12, 36          ; width in columns, height in rows
                defw    0, 0, 0         ; close, scroll, scroll to: unused
                defw    Tick            ; once an idle frame, while in front

Init:
                ld      hl,0
                ld      (Secs),hl
                xor     a
                ld      (Running),a
                ret

; A = whole seconds since the last tick. Most frames that is 0,
; and the window is only repainted when the time has changed.
Tick:
                or      a
                ret     z
                ld      e,a
                ld      a,(Running)
                or      a
                ret     z
                ld      d,0
                ld      hl,(Secs)
                add     hl,de
                ld      (Secs),hl
                jp      ApiWinRefresh

Draw:
                ld      hl,(Secs)
                ld      de,Text
                ld      bc,-600
                call    Digit           ; tens of minutes
                ld      bc,-60
                call    Digit
                inc     de              ; past the colon
                ld      bc,-10
                call    Digit
                ld      a,l
                add     a,'0'
                ld      (de),a
                ld      hl,Text
                ld      bc,4*256+2      ; 4 rows down, 2 columns in
                call    ApiWinPrint
                ld      hl,Help
                ld      bc,14*256+1
                jp      ApiWinPrint

; HL = what is left, BC = minus the place value, DE = where the
; digit goes. Past 99:59 the first digit runs on into letters.
Digit:
                ld      a,'0'-1
DigitLoop:
                inc     a
                add     hl,bc
                jr      c,DigitLoop
                sbc     hl,bc           ; carry is clear: put one back
                ld      (de),a
                inc     de
                ret

Key:
                cp      KEY_ENTER
                jr      z,Toggle
                cp      KEY_DOWN
                ret     nz
                call    Init
                jp      ApiWinRefresh
Toggle:
                ld      a,(Running)
                xor     1
                ld      (Running),a
                ret

Secs:           defw    0
Running:        defb    0
STATE           equ     $-Secs
Text:           defb    "00:00",0
Title:          defb    "STOPWATCH",0
Help:           defb    "ENTER GO",0
