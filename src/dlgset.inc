; ============================================================
;  G1: settings panel
;  The first application, and the point of it was never the
;  toggles. It is that a click on a menu item reaches storage and
;  comes back, through the event queue, the control table, save
;  under and the storage registry, without any of those layers
;  knowing about the others.
;
;  It is now also D2's first customer. Everything that used to be
;  here, the per line paint, the pixel row hit test, the cycle
;  with its special case for SPEED, is in panel.inc, and what is
;  left is the table below plus the three things that are
;  genuinely about settings rather than about panels: pushing a
;  change into the running system, saving the record, and the one
;  setting whose effect is underneath the panel.
;
;  It borrows the menu's save under buffer, which is free because
;  a menu is always closed before this opens. One transient
;  surface at a time is a real constraint and this is the first
;  place it bites; a dialogue over a menu would need a stack.
; ============================================================
DLGX            equ     9
DLGY            equ     40
DLGW            equ     16              ; SUMAXW, and B3 widened that from 12
DLGH            equ     68             ; a row taller, for the sound line
DLGROW0         equ     DLGY+10         ; first line of content
DLGLINES        equ     7
DLGLATTICE      equ     2               ; DESKTOP, the row whose effect is behind the panel

DlgOpen:
                call    PnlUpInc
                ld      a,DLGX
                ld      (SuX),a
                ld      (PnlX),a
                ld      a,DLGY
                ld      (SuY),a
                ld      (PnlY),a
                ld      a,DLGW
                ld      (SuW),a
                ld      (PnlW),a
                ld      a,DLGH
                ld      (SuH),a
                ld      (PnlH),a
                ld      a,DLGROW0
                ld      (PnlRow0),a
                ld      a,DLGLINES
                ld      (PnlRows),a
                ld      hl,DlgRows
                ld      (PnlTab),hl
                ld      hl,TxtDlgTitle
                ld      (PnlTitle),hl
                ld      hl,DlgAfter
                ld      (PnlAfter),hl
                xor     a
                ld      (PnlFocus),a            ; the keyboard starts on SPEED
                call    PnlCurEnd
                call    PtrHide                 ; keep the pointer out of it
                call    SaveUnder
                call    PnlPaint
                jp      PtrShow

DlgClose:
                ld      a,(DlgUp)
                or      a
                ret     z
                call    PnlUpDec
                ld      hl,0
                ld      (PnlAfter),hl           ; nothing owns the panel now
                call    PtrHide
                call    RestoreUnder
                jp      PtrShow

; ------------------------------------------------------------
;  A press, at pixel row C, and a key, in A. Both go through the
;  panel and come back out through DlgAfter, so the mouse and the
;  keyboard cannot end up doing different things.
; ------------------------------------------------------------
; DlgUp counts how many panels are up, not whether this one is. G3
; opens two more against the same record and D3 opens a confirm on top
; of those, and all of them are modal in exactly the same way. So
; these are the panel entry points rather than the settings panel's,
; and what they act on is whatever PnlTab is pointing at.
;
; A count rather than a flag because a confirm closing over a file
; list must not report that nothing is up. It did, briefly, and the
; symptom would have been a file list still on screen and deaf, with
; every key going to the notepad behind it.
DlgClick:
                push    bc                      ; PtrHide repaints, so it
                call    PtrHide                 ; clobbers the pixel row
                pop     bc
                call    PnlClick
                jp      PtrShow

DlgKey:
                push    af
                call    PtrHide
                pop     af
                call    PnlKey
                jp      PtrShow

; ------------------------------------------------------------
;  A = the row that was activated. Called by the panel after any
;  cycle or action, including the one that closes it.
; ------------------------------------------------------------
DlgAfter:
                ld      (DlgRow),a
                ld      a,(DlgUp)
                or      a
                ret     z                       ; DONE closed it; nothing to paint
                call    SetApply                ; one path from record to behaviour
                ld      a,(DlgRow)
                cp      DLGLATTICE
                jr      z,DlgaLattice
                ld      a,(DlgRow)
                jp      PnlRowPaint             ; only the row that changed
DlgaLattice:
                ; The lattice is the one setting whose effect is underneath
                ; the panel. Repainting only the row would leave the old
                ; desktop showing until the next window move, so the save
                ; under is spent, the desktop repainted, and the surface
                ; taken again before the panel goes back on top of it.
                call    RestoreUnder            ; the buffer is now stale
                call    DrawDesktop
                call    WinDraw                 ; the window is on top of it
                call    NoteSync                ; regrab only if text is pending
                call    SaveUnder               ; take the surface again
                jp      PnlPaint

; ------------------------------------------------------------
;  The one settings action that is not the panel's own. SAVE
;  reports through DlgResult rather than putting anything on
;  screen, because there is nowhere on a twelve column panel to
;  put a message and D3 is where an alert belongs.
; ------------------------------------------------------------
DlgSave:
                call    SetSave
                ld      a,0
                jr      nc,DlgsOk
                ld      a,1
DlgsOk:
                ld      (DlgResult),a
                ret

; ------------------------------------------------------------
;  The panel, as a table. Adding a setting is a row here and a
;  pair of names below it, and no code at all.
; ------------------------------------------------------------
DlgRows:
                defb    PT_CYCLE
                defw    TxtDlgSpeed, SetSpeed
                defb    3
                defw    NmSpeed

                defb    PT_CYCLE
                defw    TxtDlgInv, SetInvertY
                defb    2
                defw    NmOffOn

                defb    PT_CYCLE
                defw    TxtDlgLat, SetLattice
                defb    3
                defw    NmDesk

                defb    PT_CYCLE
                defw    TxtDlgKeys, SetKeyPtr
                defb    3
                defw    NmKeys

                defb    PT_CYCLE
                defw    TxtDlgSnd, SetSound
                defb    2
                defw    NmOffOn

                defb    PT_ACTION
                defw    TxtDlgSave, DlgSave
                defb    0
                defw    0

                defb    PT_ACTION
                defw    TxtDlgDone, DlgClose
                defb    0
                defw    0

; Fourteen usable columns now, so the labels that were cut down to
; five to fit a twelve column panel come back. Eight for a label, one
; of air, five for a value.
TxtDlgTitle:    defb    "SETTINGS",0
TxtDlgSpeed:    defb    "SPEED",0
TxtDlgInv:      defb    "INVERT Y",0
TxtDlgLat:      defb    "DESKTOP",0
TxtDlgSnd:      defb    "SOUND",0
TxtDlgKeys:     defb    "KEY PTR",0
TxtDlgSave:     defb    "SAVE",0
TxtDlgDone:     defb    "DONE",0

TxtOff:         defb    "OFF",0
TxtOn:          defb    "ON",0
TxtSlow:        defb    "SLOW",0
TxtMed:         defb    "MED",0
TxtFast:        defb    "FAST",0
TxtExt:         defb    "EXT",0
TxtAll:         defb    "ALL",0
TxtPlain:       defb    "PLAIN",0
TxtDots:        defb    "DOTS",0
TxtImage:       defb    "IMAGE",0

NmOffOn:        defw    TxtOff, TxtOn
NmSpeed:        defw    TxtSlow, TxtMed, TxtFast
NmKeys:         defw    TxtOff, TxtExt, TxtAll
NmDesk:         defw    TxtPlain, TxtDots, TxtImage

DlgUp:          defb    0               ; panels up, not a flag

PnlUpInc:
                ld      hl,DlgUp
                inc     (hl)
                ret
PnlUpDec:
                ld      hl,DlgUp
                ld      a,(hl)
                or      a
                ret     z
                dec     (hl)
                ret
DlgRow:         defb    0
DlgResult:      defb    0
