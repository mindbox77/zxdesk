; ============================================================
;  G3: file manager
;  Two panels, and no new visual vocabulary at all. A file is a
;  row and the list of files is a D2 panel with its labels filled
;  in at run time. The design plan and its self critique are in
;  docs/filemanager-design.md.
;
;  The point of the package is the line it does not contain: the
;  list is built from StDir, a fifth vector in the same patched
;  JP table as open, close, read and write, so nothing here knows
;  what a RAM heap is and swapping the backend swaps the list.
; ============================================================
FMFILES         equ     8               ; what the larger backend holds
FMROWS          equ     FMFILES+1       ; and one for CANCEL
FMNAMELEN       equ     RAMNAMESZ       ; StDir names, zero padded

; ------------------------------------------------------------
;  The open panel
;  Rows are built from the backend every time it opens, because
;  the directory can have changed since the last time and a
;  cached list is a second copy of the truth.
; ------------------------------------------------------------
; The same panel, entered two ways. Which way decides what pressing
; a row does and what the title says, so it is a mode you enter
; deliberately and that names itself, rather than one you fall into.
; docs/dialog-design.md argues that distinction; the short version is
; that OPEN and DELETE are two menu items exactly as OPEN and SAVE AS
; are.
FmOpenPanel:
                call    NoteIsFront     ; F2: OPEN loads into the document in
                ret     nz              ; front, so there has to be one
                xor     a
                ld      (FmMode),a
                jr      FmPanel

FmDeletePanel:
                ld      a,1
                ld      (FmMode),a
FmPanel:
                call    FmBuildRows
                call    PnlUpInc                ; the panel is modal the same
                ld      a,1                     ; way the settings one is
                ld      (FmUp),a
                ld      a,DLGX
                ld      (SuX),a
                ld      (PnlX),a
                ld      a,DLGY
                ld      (SuY),a
                ld      (PnlY),a
                ld      a,DLGW
                ld      (SuW),a
                ld      (PnlW),a
                ld      a,(FmRowCount)          ; a panel as tall as its rows,
                add     a,a                     ; because eight files and a
                add     a,a                     ; CANCEL do not fit a height
                add     a,a                     ; that was fixed for six
                add     a,DLGROW0-DLGY+2
                ld      (SuH),a
                ld      (PnlH),a
                ld      a,DLGROW0
                ld      (PnlRow0),a
                ld      a,(FmRowCount)
                ld      (PnlRows),a
                ld      hl,FmRows
                ld      (PnlTab),hl
                ld      hl,TxtFmTitle
                ld      a,(FmMode)
                or      a
                jr      z,FmpTitle
                ld      hl,TxtFmDelTitle
FmpTitle:
                ld      (PnlTitle),hl
                ld      hl,0
                ld      (PnlAfter),hl           ; every row acts on itself
                xor     a
                ld      (PnlFocus),a
                call    PnlCurEnd
                call    PtrHide
                call    SaveUnder
                call    PnlPaint
                jp      PtrShow

; ------------------------------------------------------------
;  Ask the backend for names until it refuses. A backend with no
;  directory refuses the first one, which gives an empty list,
;  and the empty list is one inert row saying so. No capability
;  test is needed to get that right.
; ------------------------------------------------------------
FmBuildRows:
                ld      hl,FmRows
                ld      (FmRowPtr),hl
                xor     a
                ld      (FmCount),a
FmbLoop:
                ld      a,(FmCount)
                cp      FMFILES
                jr      nc,FmbDone
                call    StDir
                jr      c,FmbDone
                ; The name is copied rather than pointed at. StDir's
                ; pointer is only good until the next call, which the RAM
                ; backend made look otherwise because its entries sit in
                ; fixed RAM. F3's do not: they are in a bank that is not
                ; paged in by the time anything paints, so holding the
                ; pointer gave eight rows all reading the last filename.
                push    hl
                ld      a,(FmCount)
                call    FmNameSlot              ; DE = this row's name store
                pop     hl
                push    de
                ld      bc,FMNAMELEN
                ldir
                ld      hl,(FmRowPtr)
                ld      (hl),PT_ACTION
                inc     hl
                pop     de
                ld      (hl),e                  ; the label is the copy
                inc     hl
                ld      (hl),d
                inc     hl
                ld      de,FmPick
                ld      (hl),e
                inc     hl
                ld      (hl),d
                inc     hl
                ld      (hl),0                  ; no limit
                inc     hl
                ld      (hl),0                  ; and no names table
                inc     hl
                ld      (hl),0
                inc     hl
                ld      (FmRowPtr),hl
                ld      hl,FmCount
                inc     (hl)
                jr      FmbLoop
FmbDone:
                ld      a,(FmCount)
                or      a
                jr      nz,FmbCancel
                ; nothing on this backend, so say so rather than show a
                ; panel with one row in it that is not about files
                ld      hl,(FmRowPtr)
                ld      (hl),PT_LABEL
                inc     hl
                ld      de,TxtFmEmpty
                ld      (hl),e
                inc     hl
                ld      (hl),d
                inc     hl
                ld      de,0
                ld      (hl),e
                inc     hl
                ld      (hl),d
                inc     hl
                ld      (hl),0
                inc     hl
                ld      (hl),0
                inc     hl
                ld      (hl),0
                inc     hl
                ld      (FmRowPtr),hl
                ld      hl,FmCount
                inc     (hl)
FmbCancel:
                ld      hl,(FmRowPtr)
                ld      (hl),PT_ACTION
                inc     hl
                ld      de,TxtFmCancel
                ld      (hl),e
                inc     hl
                ld      (hl),d
                inc     hl
                ld      de,FmClose
                ld      (hl),e
                inc     hl
                ld      (hl),d
                inc     hl
                ld      (hl),0
                inc     hl
                ld      (hl),0
                inc     hl
                ld      (hl),0
                ld      a,(FmCount)
                inc     a
                ld      (FmRowCount),a
                ret

; A = row index. Returns DE = where that row's name is kept.
FmNameSlot:
                ld      l,a
                ld      h,0
                ld      de,FMNAMELEN
                ld      b,a
                ld      hl,FmNames
                or      a
                jr      z,FmnDone
FmnLoop:
                add     hl,de
                djnz    FmnLoop
FmnDone:
                ex      de,hl
                ret

; ------------------------------------------------------------
;  A file row was activated. PnlIdx says which, and the row's
;  index is also its index in the directory, because the rows
;  were built in the order StDir returned them.
; ------------------------------------------------------------
FmPick:
                ld      a,(PnlIdx)
                ld      (FmChosen),a
                call    StDir
                jr      c,FmClose               ; it went away since the paint
                ld      de,FmName
                ld      bc,FMNAMELEN
                ldir                            ; the entry is the backend's,
                                                ; and closing the panel may
                                                ; well be about to move it
                ld      a,(FmMode)
                or      a
                jr      nz,FmAsk
                call    FmClose
                ld      hl,FmName
                call    NoteLoadAs
                jp      NoteRefresh

; Delete asks first, and the confirm opens over the file list rather
; than replacing it. That is what B3 and the panel record stack are
; for: if the answer is no, the list is still there and still usable.
FmAsk:
                ld      hl,TxtFmDelAsk
                ld      de,FmName
                ld      bc,FmDeleteAnswer
                jp      DlgConfirm

; A = 1 for yes. The list is rebuilt rather than patched, because a
; deleted row changes every index after it and a stale list that
; still shows the file is worse than a flicker.
FmDeleteAnswer:
                or      a
                ret     z
                ld      hl,FmName
                call    StDelete
                call    FmClose
                jp      FmDeletePanel

FmClose:
                ld      a,(FmUp)
                or      a
                ret     z
                xor     a
                ld      (FmUp),a
                call    PnlUpDec
                ld      hl,0
                ld      (PnlAfter),hl
                call    PtrHide
                call    RestoreUnder
                jp      PtrShow

; ------------------------------------------------------------
;  The save as panel. A label, a name field on its own row
;  because ten usable columns cannot hold both, and OK.
; ------------------------------------------------------------
FmSavePanel:
                call    NoteIsFront     ; and SAVE AS saves it
                ret     nz
                ld      hl,FmSaveRows
                ld      de,TxtFmSaveTitle
                ld      a,3
                call    FmNameSetup
                ; seed the box with the name the note already has, so
                ; saving again is ENTER twice rather than typing it out
                ld      hl,NoteName
                ld      de,FmField
FmsSeed:
                ld      a,(hl)
                ld      (de),a
                or      a
                jr      z,FmsSeeded
                inc     hl
                inc     de
                jr      FmsSeed
FmsSeeded:
                ld      a,(PnlFocus)            ; the cursor goes after the
                call    PnlCurEnd               ; seeded name, not before it
                call    PtrHide
                call    SaveUnder
                call    PnlPaint
                jp      PtrShow

; FILE, LOAD: a name and where from. Runs an app file.
FmLoadPanel:
                ld      hl,FmLoadRows
                ld      de,TxtFmLoadTitle
                ld      a,5
                call    FmNameSetup
                xor     a
                ld      (FmField),a
                jr      FmsSeeded

; HL = rows, DE = title, A = row count. Focus starts in the field.
FmNameSetup:
                ld      (PnlTab),hl
                ld      (PnlTitle),de
                ld      (PnlRows),a
                call    PnlUpInc
                ld      a,1
                ld      (FmUp),a
                ld      (PnlFocus),a
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
                ld      hl,0
                ld      (PnlAfter),hl
                ret

; The backend is put back before anything is drawn.
FmDoLoad:
                ld      a,(FmField)
                or      a
                ret     z
                call    FmClose
                call    StIdent
                push    af
                ld      a,(FmFrom)
                or      a
                ld      a,ST_TAPE
                call    nz,StSelect
                ld      hl,FmField
                call    LdrLoadName
                ld      b,a
                pop     af
                push    bc
                call    StSelect
                pop     af
                cp      2
                ret     nz                      ; loaded, or its own alert
                ld      hl,TxtFmNoLoad
                ld      de,0
                jp      DlgAlert

FmLoadRows:
                defb    PT_LABEL
                defw    TxtFmName, 0
                defb    0
                defw    0

                defb    PT_FIELD
                defw    0, FmField
                defb    FMFIELDLEN
                defw    0

                defb    PT_CYCLE
                defw    TxtFmFrom, FmFrom
                defb    2
                defw    NmFmFrom

                defb    PT_ACTION
                defw    TxtFmLoad, FmDoLoad
                defb    0
                defw    0

                defb    PT_ACTION
                defw    TxtFmExit, FmClose
                defb    0
                defw    0

NmFmFrom:       defw    TxtFmDisk, TxtFmTape
FmFrom:         defb    0
TxtFmLoadTitle: defb    "LOAD APP",0
TxtFmFrom:      defb    "FROM",0
TxtFmDisk:      defb    "DISK",0
TxtFmTape:      defb    "TAPE",0
TxtFmLoad:      defb    "LOAD",0
TxtFmExit:      defb    "EXIT",0
TxtFmNoLoad:    defb    "COULD NOT LOAD",0

FmDoSave:
                ld      a,(FmField)
                or      a
                ret     z                       ; an empty name saves nothing
                ld      hl,FmField
                call    NoteSaveAs
                ld      a,0
                jr      nc,FmdOk
                ld      a,1
FmdOk:
                ld      (NoteResult),a
                jp      FmClose

FmSaveRows:
                defb    PT_LABEL
                defw    TxtFmName, 0
                defb    0
                defw    0

                defb    PT_FIELD
                defw    0, FmField
                defb    FMFIELDLEN
                defw    0

                defb    PT_ACTION
                defw    TxtFmOk, FmDoSave
                defb    0
                defw    0

FMFIELDLEN      equ     10              ; a tape name. B3 widened the panel
                                        ; and RAMNAMESZ widened the backend,
                                        ; so this is the length a name is
                                        ; rather than the length that fitted

TxtFmTitle:     defb    "FILES",0
TxtFmDelTitle:  defb    "DELETE",0
TxtFmDelAsk:    defb    "DELETE FILE?",0
TxtFmSaveTitle: defb    "SAVE AS",0
TxtFmEmpty:     defb    "NO FILES",0
TxtFmCancel:    defb    "CANCEL",0
TxtFmName:      defb    "NAME",0
TxtFmOk:        defb    "OK",0

FmUp:           defb    0
FmMode:         defb    0               ; 0 open, 1 delete
FmCount:        defb    0
FmRowCount:     defb    0
FmChosen:       defb    0
FmRowPtr:       defw    0
FmName:         defs    FMNAMELEN
FmField:        defs    FMFIELDLEN+1
FmRows:         defs    FMROWS*PTSIZE
FmNames:        defs    FMFILES*FMNAMELEN
