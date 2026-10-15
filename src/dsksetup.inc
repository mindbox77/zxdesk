; ============================================================
;  Desktop setup
;  Which shortcuts are on the desktop, as a panel of toggles.
;  Where they sit is not here: that is what dragging them is for,
;  and a panel with six pairs of coordinates in it would be a
;  worse way to say the same thing than picking the icon up.
;
;  The rows point straight at the present byte of each entry,
;  because D2's PT_CYCLE takes an address and DskTab is at a
;  fixed one. There is no copy of the state to keep in step, and
;  that is the whole reason the table is laid out the way it is.
;
;  In the slow region: a panel is drawn when a menu item is
;  picked and never inside a frame.
; ============================================================
DSKSX           equ     9
DSKSY           equ     32
DSKSW           equ     16
DSKSLINES       equ     8
DSKSROW0        equ     DSKSY+10
DSKSH           equ     DSKSROW0-DSKSY+DSKSLINES*8+4

DskSetup:
                call    PnlUpInc
                ld      a,DSKSX
                ld      (SuX),a
                ld      (PnlX),a
                ld      a,DSKSY
                ld      (SuY),a
                ld      (PnlY),a
                ld      a,DSKSW
                ld      (SuW),a
                ld      (PnlW),a
                ld      a,DSKSH
                ld      (SuH),a
                ld      (PnlH),a
                ld      a,DSKSROW0
                ld      (PnlRow0),a
                ld      a,DSKSLINES
                ld      (PnlRows),a
                ld      hl,DskSetRows
                ld      (PnlTab),hl
                ld      hl,TxtDskSetup
                ld      (PnlTitle),hl
                ld      hl,DskSetAfter
                ld      (PnlAfter),hl
                xor     a
                ld      (PnlFocus),a
                call    PnlCurEnd
                call    PtrHide
                call    SaveUnder
                call    PnlPaint
                jp      PtrShow

; Any row activated. A toggle changes what the desktop should look
; like, so the desktop is repainted underneath and the panel is put
; back over it, which is the same order DlgAfter uses for the lattice
; row and for the same reason.
DskSetAfter:
                cp      DSKSLINES-2
                ret     nc                      ; SAVE and DONE do their own
                ; The same shape the lattice row uses in dlgset.inc, and for
                ; the same reason: what changed is underneath the panel, so
                ; the save under is spent, the desktop repainted and the
                ; surface taken again before the panel goes back over it.
                call    RestoreUnder
                call    DrawDesktop
                call    WinDrawLive             ; dlgset.inc
                call    SaveUnder
                jp      PnlPaint

DskSetSave:
                jp      DskSave

DskSetDone:
                call    PnlUpDec
                call    PtrHide
                call    RestoreUnder
                call    PnlPop
                call    PtrShow
                jp      WndRepaintAll

DskSetRows:
                defb    PT_CYCLE
                defw    TxtDskNote, DskTab+0*DSKENTSZ
                defb    2
                defw    NmOffOn

                defb    PT_CYCLE
                defw    TxtDskClock, DskTab+1*DSKENTSZ
                defb    2
                defw    NmOffOn

                defb    PT_CYCLE
                defw    TxtDskCal, DskTab+2*DSKENTSZ
                defb    2
                defw    NmOffOn

                defb    PT_CYCLE
                defw    TxtDskFiles, DskTab+3*DSKENTSZ
                defb    2
                defw    NmOffOn

                defb    PT_CYCLE
                defw    TxtDskAbout, DskTab+4*DSKENTSZ
                defb    2
                defw    NmOffOn

                defb    PT_CYCLE
                defw    TxtDskSet, DskTab+5*DSKENTSZ
                defb    2
                defw    NmOffOn

                defb    PT_ACTION
                defw    TxtDskSaveIt, DskSetSave
                defb    0
                defw    0

                defb    PT_ACTION
                defw    TxtDskDone, DskSetDone
                defb    0
                defw    0

TxtDskSetup:    defb    "DESKTOP",0
TxtDskSaveIt:   defb    "SAVE",0
TxtDskDone:     defb    "DONE",0
