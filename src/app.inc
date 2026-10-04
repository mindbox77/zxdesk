; ============================================================
;  F2: the application model
;  The notepad was not an application, it was a set of globals:
;  one NoteBuf, one cursor, one name. Opening a second one meant
;  a second set, and WndOpen knew both windows by name, with the
;  about window's geometry written into an IF on WinKind.
;
;  An application is a descriptor now: a constant table of where
;  its state lives, how big that state is, and the three routines
;  a window needs from whatever owns it. WndOpen reads geometry
;  and title from it rather than branching, so a third
;  application is a table entry and no edit to the window code.
;
;  There is no teardown vector, and that is F1's doing rather
;  than an omission. Everything an application allocates it takes
;  under the owning window's index, so WndClose is HeapFreeOwner
;  and the application is never asked. A vector that every
;  application would implement as RET is not a vector.
;
;  State is swapped rather than indirected through, for the same
;  reason B4 swaps the window record and D3 swaps the panel:
;  NoteCX and its neighbours are read from forty places, and a
;  pointer indirection at each is forty edits and a slower
;  editor. The live copy stays where it is and WndSelect moves
;  the bytes, which is nought edits and one LDIR.
; ============================================================
APP_STSZ        equ     0       ; word: bytes of per instance state
APP_LIVE        equ     2       ; word: where the live copy of it sits
APP_INIT        equ     4       ; word: called once, on a fresh instance
APP_DRAW        equ     6       ; word: paint the interior. The record is live
APP_KEY         equ     8       ; word: A = the key. Zero drops it
APP_TITLE       equ     10      ; word
APP_X           equ     12      ; where the first instance opens, in byte
APP_Y           equ     13      ; columns and pixel rows
APP_W           equ     14
APP_H           equ     15
; The teardown vector F2 said it did not need. It said so because
; everything an application allocates comes back through the owner
; byte, and that is still true: this one is not about memory, it is
; about consent. Carry set means "not yet", and the notepad uses it to
; put a question on screen and close later, from the answer.
;
; It is last in the record rather than next to the other vectors so
; that WndOpen's walk from APP_TITLE to APP_X is untouched.
APP_CLOSE       equ     16
; The scroll model: a vector answering how many units there are, how
; many are showing and which is first. The window frame draws a bar
; from that and has no business knowing a unit is a line of text in
; one window and a filename in another. Null means no bar.
APP_SCROLL      equ     18
; And the other half of it: A = the unit to make first. Asking where
; the view is and never being able to move it would be an indicator
; rather than a control, and clicking an arrow that does nothing is
; worse than having no arrow.
APP_SCROLLTO    equ     20
; Called once an idle frame while the window is in front.
; A = whole seconds since the last call. Null means no tick.
APP_TICK        equ     22
APPSIZE         equ     24

APP_NOTE        equ     0
APP_ABOUT       equ     1
APP_CLOCK       equ     2
APP_CAL         equ     3
APP_CMD         equ     4
APP_KEYS        equ     5
APP_EXT         equ     6       ; the first loaded app, see loader.inc
LDRSLOTS        equ     3       ; how many can be loaded at once

AppTable:
AppNote:        defw    NOTESTSZ, NoteState, NoteInit, NoteDrawInterior
                defw    NoteKeyPaint, TxtWinTitle
                defb    8, 48, 18, 72   ; two columns wider than it was: the
                                        ; bar takes one and the document is
                                        ; still fourteen characters
                defw    NoteClose
                defw    NoteScrollInfo
                defw    NoteScrollTo
                defw    0
                ; The about window has no state, so its size is nought and
                ; both halves of the swap are a compare and a return. Its
                ; key vector is null, which says what an explicit test on
                ; the kind used to say: it drops what is typed at it.
AppAbout:       defw    0, 0, 0, WdAbout
                defw    0, TxtAboutTitle
                defb    14, 92, 15, 52  ; 11 characters and a clear
                                        ; column each side
                defw    0                       ; nothing to lose, so nothing
                                                ; to ask
                defw    0, 0
                defw    0
                ; The clock keeps nothing per window either, and for a
                ; better reason than the about window's: the time belongs
                ; to the machine, so two clock windows showing different
                ; times would be two clocks rather than two views.
AppClock:       defw    0, 0, 0, ClkDraw
                defw    ClkKey, TxtClockTitle
                defb    18, 40, CLKW, CLKH
                defw    0
                defw    0, 0
                defw    0
                ; The calendar does, and it is the smallest state in the
                ; system at two bytes, which is the point of it being a
                ; heap block rather than a special case.
AppCal:         defw    CALSTSZ, CalState, CalInit, CalDraw
                defw    CalKey, TxtCalTitle
                defb    4, 32, CALW, CALH
                defw    0
                defw    0, 0
                defw    0
                ; The commander. A window rather than a panel because a
                ; panel thirty columns wide would have wanted a save under
                ; arena nearly three kilobytes larger, and a window takes
                ; its pixels from the heap sized to itself.
AppCmd:         defw    CMDSTSZ, CmdState, CmdInit, CmdDraw
                defw    CmdKey, TxtCmdTitle
                defb    1, 24, CMDW, CMDH
                defw    0
                defw    CmdScrollInfo
                defw    CmdScrollTo
                defw    0
                ; The shortcut list. No state, no key vector and no close
                ; vector: there is nothing to type at a list and nothing
                ; to lose by shutting it.
AppKeys:        defw    0, 0, 0, KeysDraw
                defw    0, TxtKeysTitle
                defb    14, 20, KEYSW, KEYSH
                defw    0
                defw    0, 0
                defw    0
                ; Filled in by the loader, one for each loaded app.
AppExt:         defs    APPSIZE*LDRSLOTS

; ------------------------------------------------------------
;  AppAt
;  in:  A = an application index
;  out: HL = its descriptor
; ------------------------------------------------------------
AppAt:
                ld      hl,AppTable
                or      a
                ret     z
                ld      b,a
                ld      de,APPSIZE
AppAtLoop:
                add     hl,de
                djnz    AppAtLoop
                ret

; ------------------------------------------------------------
;  AppCall
;  in:  C = one of the APP_ offsets, A = whatever that vector
;       wants
;  Calls it through the live window's descriptor, or returns
;  having done nothing if the vector is null. The offset is in C
;  rather than A because the key vector needs A, and a routine
;  that takes two things in one register is the shape of the two
;  bugs B4 cost.
; ------------------------------------------------------------
AppCall:
                push    af
                ld      hl,(WinApp)
                ld      a,h
                or      l
                jr      z,AcNone
                ld      e,c
                ld      d,0
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ld      a,d
                or      e
                jr      z,AcNone
                pop     af
                ex      de,hl
                jp      (hl)
AcNone:
                pop     af
                ret

; ------------------------------------------------------------
;  AppSave, AppLoad
;  The state of the window going out, written back to its own
;  block, and the state of the window coming in, read into the
;  live copy. This is what makes a second notepad a second
;  document rather than a second view of the same one.
; ------------------------------------------------------------
AppSave:
                call    AppStateAt
                ret     z
                ldir                            ; live into the block
                ret

AppLoad:
                call    AppStateAt
                ret     z
                ex      de,hl                   ; and the block into live
                ldir
                ret

; out: HL = the live copy, DE = this window's block, BC = the size,
;      Z set if there is nothing to move either way
AppStateAt:
                ld      hl,(WinApp)
                ld      a,h
                or      l
                ret     z                       ; no application at all
                ld      c,(hl)
                inc     hl
                ld      b,(hl)
                inc     hl                      ; BC = the state size
                ld      a,b
                or      c
                ret     z                       ; an application that keeps
                                                ; nothing, which is the about
                                                ; window
                ld      e,(hl)
                inc     hl
                ld      d,(hl)                  ; DE = the live copy
                push    de
                ld      de,(WinStateP)
                ld      a,d
                or      e                       ; Z if it never got a block
                pop     hl                      ; HL = the live copy
                ret

; ------------------------------------------------------------
;  AppTick
;  Once an idle frame, to the app in front. A = seconds since
;  the last one. Held back while anything is drawn over it.
; ------------------------------------------------------------
AppTick:
                ld      a,(Dragging)
                ld      hl,Resizing
                or      (hl)
                ld      hl,DlgUp
                or      (hl)
                ld      hl,MenuOpen
                or      (hl)
                ret     nz
                call    WndIsFront
                ret     nz
                ld      hl,TickSecs
                ld      a,(hl)
                ld      (hl),0
                ld      c,APP_TICK
                jp      AppCall

TickSecs:       defb    0
