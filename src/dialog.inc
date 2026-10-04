; ============================================================
;  D3: dialogues
;  A dialogue is a panel with a question in it. That is the whole
;  package, and it is short because D2 was extracted properly; if
;  this file were long, D2 would have been wrong.
;
;  Not a modal loop. The roadmap asked for one, which is how a
;  machine with a main loop and no event queue does this. This
;  one has an event queue and a panel that already takes every
;  press and every key while it is up, so a confirm does not
;  block: it opens, and it calls back with the answer when it
;  closes.
;
;  The design plan and its self critique are in
;  docs/dialog-design.md.
; ============================================================
DGX           equ     10              ; a little inside the panels it covers
DGY           equ     52
DGW           equ     14
DGROW0        equ     DGY+10
; Five rows, because DlgSave3 has three answers. A confirm uses four
; of them and the panel is drawn to DgCount rather than to this, so the
; extra row costs height on the tallest dialogue only.
DGH           equ     DGROW0-DGY+5*8+2

; ------------------------------------------------------------
;  DlgConfirm
;  in: HL = first line, DE = second line or 0, BC = the routine
;      to call afterwards with A = 1 for yes and 0 for no
;
;  NO is the first of the two and the focus starts on it. A
;  confirm exists because the action is hard to undo, and the row
;  the bar is already sitting on is the row a hurried ENTER will
;  take, so the safe answer has to be the one sitting there.
; ------------------------------------------------------------
DlgConfirm:
                ld      (DgLine1),hl
                ld      (DgLine2),de
                ld      (DgAfter),bc
                ld      hl,DgConfirmRows
                ld      (DgTab),hl
                ld      a,4
                ld      (DgCount),a
                ld      a,2                     ; NO, not YES
                ld      (DgFocus),a
                ld      hl,TxtDdConfirm
                ld      (DgTitle),hl
                jr      DgOpen

; ------------------------------------------------------------
;  DlgSave3
;  in: HL = first line, DE = second line or 0, BC = the routine
;      to call afterwards with A = 0 cancel, 1 discard, 2 save
;
;  Two answers were enough while the only question was whether to
;  delete a file, where NO means "do nothing" and covers both
;  ways of declining. Closing a window with unsaved work has
;  three answers and not two: keep the work by saving it, throw
;  it away, or do not close after all. Collapsing any pair of
;  those loses something a person meant.
;
;  CANCEL is first and the bar starts on it, for the same reason
;  NO does in a confirm: the row the bar is already on is the row
;  a hurried ENTER takes, so it has to be the one that changes
;  nothing.
; ------------------------------------------------------------
DlgSave3:
                ld      (DgLine1),hl
                ld      (DgLine2),de
                ld      (DgAfter),bc
                ld      hl,DgSaveRows
                ld      (DgTab),hl
                ld      a,5
                ld      (DgCount),a
                ld      a,2                     ; CANCEL
                ld      (DgFocus),a
                ld      hl,TxtDdSave
                ld      (DgTitle),hl
                jr      DgOpen

; ------------------------------------------------------------
;  DlgAlert
;  in: HL = first line, DE = second line or 0
;  Nothing to answer, so nothing to call back.
; ------------------------------------------------------------
DlgAlert:
                ; The one that earns the whole feature. An alert is the
                ; system telling you something went wrong, and one that
                ; appears silently while you are looking at the keyboard
                ; has not told you anything.
                push    hl
                push    de                      ; SndPlay uses DE
                ld      a,SND_BEEP
                call    SndPlay
                pop     de
                pop     hl
                ld      (DgLine1),hl
                ld      (DgLine2),de
                ld      hl,0
                ld      (DgAfter),hl
                ld      hl,DgAlertRows
                ld      (DgTab),hl
                ld      a,3
                ld      (DgCount),a
                ld      a,2                     ; OK
                ld      (DgFocus),a
                ld      hl,TxtDdAlert
                ld      (DgTitle),hl
                ; fall through

DgOpen:
                ; the two message rows are labels whose text is patched in,
                ; the same trick the file list uses for its filenames
                ld      hl,(DgTab)
                inc     hl
                ld      de,(DgLine1)
                ld      (hl),e
                inc     hl
                ld      (hl),d
                ld      hl,(DgTab)
                ld      de,PTSIZE+1
                add     hl,de
                ld      de,(DgLine2)
                ld      (hl),e
                inc     hl
                ld      (hl),d

                call    PnlPush                 ; the panel underneath, if any
                call    PnlUpInc                ; and it is modal like the rest
                ld      hl,DgDepth
                inc     (hl)

                ld      a,DGX
                ld      (SuX),a
                ld      (PnlX),a
                ld      a,DGY
                ld      (SuY),a
                ld      (PnlY),a
                ld      a,DGW
                ld      (SuW),a
                ld      (PnlW),a
                ld      a,DGH
                ld      (SuH),a
                ld      (PnlH),a
                ld      a,DGROW0
                ld      (PnlRow0),a
                ld      a,(DgCount)
                ld      (PnlRows),a
                ld      hl,(DgTab)
                ld      (PnlTab),hl
                ld      hl,(DgTitle)
                ld      (PnlTitle),hl
                ld      hl,0
                ld      (PnlAfter),hl           ; each row acts on itself
                ld      a,(DgFocus)
                ld      (PnlFocus),a
                call    PnlCurEnd
                call    PtrHide
                call    SaveUnder
                call    PnlPaint
                jp      PtrShow

; ------------------------------------------------------------
;  The three answers. Each closes the dialogue and then calls
;  back, in that order, because the callback usually paints and
;  it must not paint underneath a panel that is still up.
; ------------------------------------------------------------
DgSave:
                ld      a,2
                jr      DgAnswer
DgDiscard:
                ld      a,1
                jr      DgAnswer
DgCancel:
                xor     a
                jr      DgAnswer
DgYes:
                ld      a,1
                jr      DgAnswer
DgNo:
                xor     a
                jr      DgAnswer
DgOk:
                xor     a
DgAnswer:
                ld      (DgResult),a
                push    af
                ld      a,SND_CLICK             ; answering is a press, and
                call    SndPlay                 ; presses click
                pop     af
                call    DgClose
                ld      hl,(DgAfter)
                ld      a,h
                or      l
                ret     z
                ld      a,(DgResult)
                jp      (hl)

DgClose:
                ld      a,(DgDepth)
                or      a
                ret     z
                dec     a
                ld      (DgDepth),a
                call    PnlUpDec
                call    PtrHide
                call    RestoreUnder            ; the pixels underneath
                call    PnlPop                  ; and the panel that owns them
                jp      PtrShow

DgConfirmRows:
                defb    PT_LABEL
                defw    0, 0
                defb    0
                defw    0

                defb    PT_LABEL
                defw    0, 0
                defb    0
                defw    0

                defb    PT_ACTION
                defw    TxtDdNo, DgNo
                defb    0
                defw    0

                defb    PT_ACTION
                defw    TxtDdYes, DgYes
                defb    0
                defw    0

DgSaveRows:
                defb    PT_LABEL
                defw    0, 0
                defb    0
                defw    0

                defb    PT_LABEL
                defw    0, 0
                defb    0
                defw    0

                defb    PT_ACTION
                defw    TxtDdCancel, DgCancel
                defb    0
                defw    0

                defb    PT_ACTION
                defw    TxtDdDiscard, DgDiscard
                defb    0
                defw    0

                defb    PT_ACTION
                defw    TxtDdSaveIt, DgSave
                defb    0
                defw    0

DgAlertRows:
                defb    PT_LABEL
                defw    0, 0
                defb    0
                defw    0

                defb    PT_LABEL
                defw    0, 0
                defb    0
                defw    0

                defb    PT_ACTION
                defw    TxtDdOk, DgOk
                defb    0
                defw    0

TxtDdConfirm:   defb    "CONFIRM",0
TxtDdAlert:     defb    "NOTICE",0
TxtDdYes:       defb    "YES",0
TxtDdNo:        defb    "NO",0
TxtDdOk:        defb    "OK",0
TxtDdSave:      defb    "UNSAVED",0
TxtDdCancel:    defb    "CANCEL",0
TxtDdDiscard:   defb    "DISCARD",0
TxtDdSaveIt:    defb    "SAVE",0

DgLine1:        defw    0
DgLine2:        defw    0
DgAfter:        defw    0
DgTab:          defw    0
DgTitle:        defw    0
DgCount:        defb    0
DgFocus:        defb    0
DgResult:       defb    0
DgDepth:        defb    0
