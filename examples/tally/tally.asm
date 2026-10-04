; Tally: an app that uses storage and a dialog.
; UP adds one. S saves the count to a file called COUNT on the
; current device, L loads it back. DOWN asks, then clears.
;
;   ./mkapp.py examples/tally/tally.asm build/tally.zxa

                include "zxdesk.inc"

                org     APPORG

                defw    1               ; bytes of state kept per window
                defw    Count
                defw    Init
                defw    Draw
                defw    Key
                defw    Title
                defb    0, 96
                defb    12, 36
                defw    0, 0, 0
                defw    0               ; tick: unused

Init:
                xor     a
                ld      (Count),a
                ret

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
                jr      z,Ask
                cp      'S'
                jr      z,Save
                cp      'L'
                ret     nz

; Open, read one byte, close. A missing file gets an alert.
                ld      b,FA_READ
                call    Open
                jr      c,NoFile
                call    ApiStRead       ; A = handle, HL = buffer, BC = count
                call    Close
                jp      ApiWinRefresh
NoFile:
                ld      hl,TxtNone
                ld      de,0
                jp      ApiDlgAlert

Save:
                ld      b,FA_OVERWRITE
                call    Open
                ret     c
                call    ApiStWrite
Close:
                ld      a,(Handle)
                jp      ApiStClose

; B = mode. Leaves A, HL and BC ready for the read or the write.
Open:
                ld      hl,Name
                call    ApiStOpen
                ret     c
                ld      (Handle),a
                ld      hl,Count
                ld      bc,1
                ret

Up:
                ld      hl,Count
                inc     (hl)
                jp      ApiWinRefresh

; The question does not wait. It returns at once, and Answer is
; called later with A = 1 for yes.
Ask:
                ld      hl,TxtAsk
                ld      de,0
                ld      bc,Answer
                jp      ApiDlgConfirm
Answer:
                or      a
                ret     z
                call    Init
                jp      ApiWinRefresh

Count:          defb    0
Handle:         defb    0
Digits:         defb    "000",0
Name:           defb    "COUNT",0
Title:          defb    "TALLY",0
Help:           defb    "UP S L",0
TxtAsk:         defb    "CLEAR IT?",0
TxtNone:        defb    "NO COUNT FILE",0
