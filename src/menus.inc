; ============================================================
;  Menus
;  The bar has been drawn since the first build but did nothing.
;  A press on a title now opens a pull down over the desktop
;  using SaveUnder, and any press afterwards closes it again by
;  restoring what was underneath.
;
;  A menu definition is six bytes: the title's byte column and
;  width on the bar, the drop down's width, the item count, and a
;  pointer to the items, which are null terminated strings laid
;  end to end.
;
;  Drop widths are all even because DevFillRect works in pairs of
;  bytes and would round an odd width down, painting nothing.
;  That has caught this project once already.
; ============================================================
MNDEFSZ         equ     6
MNITEMH         equ     8               ; pixel rows per item

MenuDefs:
                defb    1,8,10,2
                defw    MnItems1
                defb    10,6,8,7
                defw    MnItems2
                defb    17,6,10,5
                defw    MnItems3
                defb    24,6,8,2
                defw    MnItems4
MNCOUNT         equ     4

MnItems1:       defb    "ABOUT",0
                defb    "SETTINGS",0
MnItems2:       defb    "NEW",0
                defb    "OPEN",0
                defb    "SAVE",0
                defb    "SAVE AS",0
                defb    "PRINT",0
                defb    "DELETE",0
                defb    "LOAD",0
; VIEW held TIDY and REFRESH, which were drawn from the first build
; and never wired to anything. Two applications that exist is a better
; use of a menu than two words that do not.
MnItems3:       defb    "CLOCK",0
                defb    "CALENDAR",0
                defb    "CASCADE",0
                defb    "TILE",0
                defb    "DESKTOP",0
MnItems4:       defb    "KEYS",0
                defb    "ABOUT",0

MenuOpen:       defb    0               ; 0 none, otherwise index+1
MnX:            defb    0
MnY:            defb    0
MnW:            defb    0
MnH:            defb    0
MnCount:        defb    0
MnItemPtr:      defw    0

; Which title is under byte column B? Returns A = index+1, or zero.
MenuHitTitle:
                ld      hl,MenuDefs
                ld      c,1
MhtLoop:
                ld      a,c
                cp      MNCOUNT+1
                jr      nc,MhtNone
                ld      a,(hl)                  ; title column
                cp      b
                jr      z,MhtIn
                jr      nc,MhtNext
MhtIn:
                inc     hl
                add     a,(hl)                  ; + its width
                dec     hl
                cp      b
                jr      z,MhtNext
                jr      c,MhtNext
                ld      a,c
                ret
MhtNext:
                ld      de,MNDEFSZ
                add     hl,de
                inc     c
                jr      MhtLoop
MhtNone:
                xor     a
                ret

; Open menu A, one based. The pointer is lifted before the save so
; its image is not captured into the buffer, and put back after the
; paint so it is not left underneath the menu.
MenuOpenDrop:
                ld      (MenuOpen),a
                dec     a
                ld      hl,MenuDefs
                or      a
                jr      z,MoGot
                ld      b,a
                ld      de,MNDEFSZ
MoSkip:
                add     hl,de
                djnz    MoSkip
MoGot:
                ld      a,(hl)                  ; title column
                ld      (MnX),a
                inc     hl
                inc     hl                      ; past the bar width
                ld      a,(hl)                  ; drop width
                ld      (MnW),a
                inc     hl
                ld      a,(hl)                  ; item count
                ld      (MnCount),a
                add     a,a
                add     a,a
                add     a,a                     ; eight rows an item
                add     a,2                     ; a border row top and bottom
                ld      (MnH),a
                inc     hl
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ld      (MnItemPtr),de
                ld      a,MENUH
                ld      (MnY),a
                ; nudge it left if it would run off the right edge
                ld      a,(MnX)
                ld      b,a
                ld      a,(MnW)
                add     a,b
                cp      SCRCOLS+1
                jr      c,MoFits
                ld      a,(MnW)
                neg
                add     a,SCRCOLS
                ld      (MnX),a
MoFits:
                ld      a,(MnX)
                ld      (SuX),a
                ld      a,(MnY)
                ld      (SuY),a
                ld      a,(MnW)
                ld      (SuW),a
                ld      a,(MnH)
                ld      (SuH),a
                call    PtrRestore              ; do not save the pointer image
                call    SaveUnder
                call    MenuPaint
                call    PtrSaveBg               ; and do not leave it buried
                jp      PtrDraw

; Close whatever is open. Nothing to do if nothing is.
MenuClose:
                ld      a,(MenuOpen)
                or      a
                ret     z
                xor     a
                ld      (MenuOpen),a
                call    PtrRestore
                call    RestoreUnder
                call    PtrSaveBg
                jp      PtrDraw

; C = pixel row. Returns the item index, or $FF if it is not on one.
MenuItemAt:
                ld      a,(MnY)
                inc     a
                ld      b,a
                ld      a,c
                sub     b
                jr      c,MiaNone
                srl     a
                srl     a
                srl     a
                ld      b,a
                ld      a,(MnCount)
                cp      b
                jr      z,MiaNone
                jr      c,MiaNone
                ld      a,b
                ret
MiaNone:
                ld      a,$FF
                ret

; Paint the drop down: a solid black rectangle, a white interior one
; row in at top and bottom, the left and right pixel columns knocked
; back to black, then the items.
MenuPaint:
                ld      a,(MnX)
                ld      (FrX),a
                ld      a,(MnY)
                ld      (FrY),a
                ld      a,(MnW)
                ld      (FrW),a
                ld      a,(MnH)
                ld      (FrH),a
                ld      hl,$FFFF
                ld      (FrPat),hl
                call    DevFillRect
                ld      a,(MnY)
                inc     a
                ld      (FrY),a
                ld      a,(MnH)
                sub     2
                ld      (FrH),a
                ld      hl,0
                ld      (FrPat),hl
                call    DevFillRect
                ld      a,(MnY)
                inc     a
                ld      (PxRow),a
                ld      a,(MnH)
                sub     2
                ld      b,a
MpEdge:
                push    bc
                ld      a,(MnX)
                ld      c,a
                ld      a,(PxRow)
                call    AddrAt
                ld      a,(hl)
                or      $80
                ld      (hl),a
                ld      a,(MnW)
                dec     a
                ld      e,a
                ld      d,0
                add     hl,de
                ld      a,(hl)
                or      $01
                ld      (hl),a
                ld      a,(PxRow)
                inc     a
                ld      (PxRow),a
                pop     bc
                djnz    MpEdge
                ld      a,(MnY)
                inc     a
                ld      (PxRow),a
                ld      hl,(MnItemPtr)
                ld      a,(MnCount)
                ld      b,a
MpItem:
                push    bc
                push    hl
                xor     a
                ld      (PrintInv),a
                ld      a,(MnX)
                inc     a
                ld      c,a
                call    PrintStrPx2
                pop     hl
MpSkip:
                ld      a,(hl)
                inc     hl
                or      a
                jr      nz,MpSkip
                ld      a,(PxRow)
                add     a,MNITEMH
                ld      (PxRow),a
                pop     bc
                djnz    MpItem
                ret

MenuPick:       defb    $FF             ; last item chosen, for tests
MnLastMenu:     defb    0                       ; which menu it came from

; A = item index, MnLastMenu = which menu. The table is walked rather
; than indexed because most entries do nothing yet and a sparse list
; is smaller than a full grid of stubs.
MenuAction:
                ; Every menu pick clicks, which is one hook rather than one
                ; per item, and it means an item added later is audible
                ; without anybody remembering to make it so.
                push    af
                ld      a,SND_CLICK
                call    SndPlay
                pop     af
MenuActGo:
                ld      c,a
                ld      a,(MnLastMenu)
                ld      b,a
                ld      hl,MnActions
MnaLoop:
                ld      a,(hl)
                cp      $FF
                ret     z
                cp      b
                jr      nz,MnaNext
                inc     hl
                ld      a,(hl)
                cp      c
                jr      nz,MnaNext2
                inc     hl
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ex      de,hl
                jp      (hl)
MnaNext2:
                dec     hl
MnaNext:
                ld      de,4
                add     hl,de
                jr      MnaLoop

; menu, item, routine
MnActions:
                defb    1,0
                defw    AboutOpen               ; ZX DESK, ABOUT
                defb    1,1
                defw    DlgOpen                 ; ZX DESK, SETTINGS
                defb    2,0
                defw    NoteMenuNew             ; FILE, NEW
                defb    2,1
                defw    CmdOpenWin              ; FILE, OPEN, which is now the
                                                ; two pane window rather than
                                                ; the one pane list
                defb    2,2
                defw    NoteMenuSave            ; FILE, SAVE, under the name
                                                ; the note already has
                defb    2,3
                defw    FmSavePanel             ; FILE, SAVE AS
                defb    2,4
                defw    NotePrint               ; FILE, PRINT
                defb    2,5
                defw    FmDeletePanel           ; FILE, DELETE
                defb    2,6
                defw    FmLoadPanel             ; FILE, LOAD, an app file
                defb    3,0
                defw    ClockOpen               ; VIEW, CLOCK
                defb    3,1
                defw    WndOpenCal              ; VIEW, CALENDAR
                defb    3,2
                defw    WndCascade              ; VIEW, CASCADE
                defb    3,3
                defw    WndTile                 ; VIEW, TILE
                defb    3,4
                defw    DskSetup                ; VIEW, DESKTOP
                defb    4,0
                defw    KeysOpen                ; HELP, KEYS
                defb    4,1
                defw    AboutOpen               ; HELP, ABOUT
                defb    $FF
