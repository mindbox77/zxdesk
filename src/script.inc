IFDEF SCRIPT
; ============================================================
;  Scripted input
;  The DEMO build proves the paint path but bypasses the event
;  queue entirely, so nothing between a button press and a moved
;  window was ever verified by machine. This replaces ReadInput
;  with a table of synthetic input states, one row per frame, so
;  a whole interaction replays identically every run and the
;  result can be read off the screen.
;
;  Table rows are: repeat count, button mask, PtrX, PtrY.
;  A repeat count of zero ends the script.
; ============================================================
ScriptStep:
                ld      a,(ScrDone)
                or      a
                ret     nz
                ld      a,(ScrRep)
                or      a
                jr      nz,ScrSame
                ld      hl,(ScrPtr)
                ld      a,(hl)
                or      a
                jr      z,SsFinish
                ld      (ScrRep),a
                inc     hl
                ld      a,(hl)
                ld      (ScrBtn),a
                inc     hl
                ld      a,(hl)
                ld      (ScrX),a
                inc     hl
                ld      a,(hl)
                ld      (ScrY),a
                inc     hl
                ld      (ScrPtr),hl
ScrSame:
                ld      a,(ScrRep)
                dec     a
                ld      (ScrRep),a
                ld      a,(ScrBtn)
                ld      (Buttons),a
                ld      a,(ScrX)
                ld      (PtrX),a
                ld      a,(ScrY)
                ld      (PtrY),a
                call    EvPoll
                jp      EvDispatch

; Print where the window actually ended up, so the run can be read
; off a screenshot instead of trusted.
SsFinish:
                ld      a,1
                ld      (ScrDone),a
IFDEF TOURDEMO
                ret                     ; the tour is watched, not read
ENDIF
                ld      hl,TxtScrDone
                ld      b,STATROW
                ld      c,0
                call    PrintStr
                ld      a,(WinX)
                ld      b,STATROW
                ld      c,3
                call    PrintDec3
                ld      a,(WinY)
                ld      b,STATROW
                ld      c,10
                call    PrintDec3
                ret

TxtScrDone:     defb    "WX     WY",0

; The script. Move onto the title bar, press, drag down twenty four
; pixels and right eight, release, then sit still.
;
; Expected finish, worked out from the drag arithmetic rather than
; from the screen: the grab records DragDX as the pointer's byte
; column minus WinX, 12 - 8 = 4, and DragDY as PtrY - WinY, 52 - 48
; = 4. The release is at PtrX 108, byte column 13, and PtrY 76, so
; WinX = 13 - 4 = 9 and WinY = 76 - 4 = 72. X moves by one column,
; not two, because the pointer crossed only one column boundary.
IFDEF TOURDEMO
; ------------------------------------------------------------
;  The tour
;  Every other script in this file is a test that happens to be
;  watchable. This one is the other way round: it is a
;  demonstration, and it exists because a desktop is a thing you
;  show rather than a thing you describe. It drives the pointer
;  through a drag, a resize both ways, three shortcuts and the
;  two window arrangements, with pauses long enough to see what
;  just happened.
;
;  The arithmetic is the same as the drag test's and worked out
;  the same way. The notepad opens at column 8 row 48, the grab
;  at PtrX 120 is byte column 15, so DragDX is 7 and a release
;  at column 19 puts WinX at 12. The grip follows the window it
;  is on: after the shrink the corner cell has moved, and the
;  second press is aimed at where it went rather than where it
;  was.
;
;  Icons are hit on the picture rather than the label, because
;  DskHit tests the icon's two columns and the label is wider
;  than that on three of the four.
; ------------------------------------------------------------
ScriptTab:
                defb    100,$FF,128,100 ; the desktop, before anything

                ; drag the notepad down and to the right
                defb    15,$FF,120,52   ; onto its title bar
                defb    5,$FD,120,52    ; press: starts the drag
                defb    4,$FD,126,60
                defb    4,$FD,132,68
                defb    4,$FD,140,76
                defb    4,$FD,148,84
                defb    4,$FD,156,96    ; column 19, so WinX 12 and WinY 92
                defb    5,$FF,156,96    ; release
                defb    20,$FF,156,96

                ; take the grip and pull it in
                defb    15,$FF,200,130
                defb    10,$FF,236,160  ; the corner cell: column 29, row 160
                defb    5,$FD,236,160
                defb    4,$FD,228,154
                defb    4,$FD,216,146
                defb    4,$FD,204,138
                defb    4,$FD,196,132   ; 13 columns by 41 rows
                defb    5,$FF,196,132
                defb    25,$FF,196,132

                ; and push it back out. The grip is where the window
                ; left it, not where it started
                defb    10,$FF,196,132
                defb    5,$FD,196,132
                defb    4,$FD,208,140
                defb    4,$FD,220,148
                defb    4,$FD,232,156
                defb    4,$FD,236,160
                defb    5,$FF,236,160
                defb    25,$FF,236,160

                ; the CLOCK shortcut
                defb    20,$FF,14,56
                defb    5,$FD,14,56
                defb    5,$FF,14,56
                defb    40,$FF,120,170  ; park the pointer off the windows

                ; the CAL shortcut
                defb    20,$FF,14,88
                defb    5,$FD,14,88
                defb    5,$FF,14,88
                defb    50,$FF,120,170

                ; and FILES, which covers the icons, so it goes last
                defb    20,$FF,14,120
                defb    5,$FD,14,120
                defb    5,$FF,14,120
                defb    60,$FF,120,170

                ; VIEW, CASCADE
                defb    20,$FF,145,4
                defb    5,$FD,145,4     ; press: opens the pull down
                defb    5,$FF,145,4
                defb    8,$FF,145,28    ; onto CASCADE, the third item
                defb    5,$FD,145,28
                defb    5,$FF,145,28
                defb    100,$FF,120,170

                ; VIEW, TILE
                defb    20,$FF,145,4
                defb    5,$FD,145,4
                defb    5,$FF,145,4
                defb    8,$FF,145,36    ; onto TILE, the fourth
                defb    5,$FD,145,36
                defb    5,$FF,145,36
                defb    100,$FF,120,170

                defb    40,$FF,120,170
                defb    0
ELSE
IFDEF PRINTDEMO
; Printing is the one thing in this system that cannot be checked
; without hardware, and Fuse has the hardware: it emulates a ZX
; Printer and writes what comes out to a file. So this script opens
; FILE and picks PRINT, and the answer is a picture on disc rather
; than a number on screen.
;
; The document is seeded at boot by the same flag, because the script
; drives the pointer and the buttons and cannot type.
ScriptTab:
                defb    2,$FF,84,4      ; onto FILE
                defb    3,$FD,84,4      ; press: opens the pull down
                defb    2,$FF,84,4
                defb    2,$FF,84,44     ; onto PRINT, the fifth item
                defb    3,$FD,84,44     ; press: picks it
                defb    2,$FF,84,44
                defb    2,$FF,84,120
                defb    200,$FF,84,120  ; four seconds for the stylus
                defb    0
ELSE
IFDEF CLOCKDEMO
; The clock has one claim the headless harness cannot check: that it
; advances on its own. zxtest.py has no interrupt source and drives
; IrqCnt by hand, so the only place the ULA actually ticks this is
; here. Open VIEW, pick CLOCK, and then sit still for five seconds:
; the screenshot should read a few seconds past noon rather than
; twelve exactly, and two runs should differ.
ScriptTab:
                defb    2,$FF,145,4     ; onto VIEW
                defb    3,$FD,145,4     ; press: opens the pull down
                defb    2,$FF,145,4     ; release
                defb    2,$FF,145,12    ; onto CLOCK, the first item
                defb    3,$FD,145,12    ; press: picks it
                defb    2,$FF,145,12
                defb    2,$FF,145,60    ; out of the way of the window
                defb    250,$FF,145,60  ; five seconds of it running
                defb    0
ELSE
ScriptTab:
                defb    2,$FF,100,52    ; on the title bar, nothing pressed
                defb    2,$FD,100,52    ; press: this must start a drag
                defb    1,$FD,100,58
                defb    1,$FD,100,64
                defb    1,$FD,100,70
                defb    1,$FD,108,76    ; and two byte columns to the right
                defb    2,$FF,108,76    ; release
                defb    2,$FF,90,4      ; onto FILE on the menu bar
                defb    3,$FD,90,4      ; press: opens the FILE pull down
                defb    2,$FF,90,4      ; release
                defb    3,$FD,200,150   ; press on the desktop: dismisses it
                defb    2,$FF,200,150   ; a menu swallows the next press, so
                defb    2,$FF,20,4      ; this one has to be its own click
                defb    3,$FD,20,4      ; press: opens ZX DESK
                defb    2,$FF,20,4
                defb    2,$FF,20,20     ; onto SETTINGS, item index 1
                defb    3,$FD,20,20     ; press: picks it, panel opens
                defb    2,$FF,20,20
                defb    2,$FF,84,50     ; onto the SPEED line
                defb    3,$FD,84,50     ; press: cycles nought to one
                defb    2,$FF,84,50
                defb    3,$FD,84,50     ; again: one to two
                defb    2,$FF,84,50
                defb    2,$FF,84,90     ; onto SAVE, which is row five now
                defb    3,$FD,84,90     ; that SOUND has taken row four
                defb    40,$FF,84,90    ; leave the panel up to look at
                defb    0
ENDIF
ENDIF
ENDIF

ScrPtr:         defw    ScriptTab
ScrRep:         defb    0
ScrBtn:         defb    $FF
ScrX:           defb    120
ScrY:           defb    90
ScrDone:        defb    0

ENDIF
