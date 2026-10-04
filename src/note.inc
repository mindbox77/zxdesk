; ============================================================
;  G2: notepad
;  The first consumer of the C3 keyboard, which until now posted
;  EV_KEY into a handler slot wired to EvIgnore. It is also the
;  package that drags a text field into existence by demand
;  rather than building a control library against no application.
;
;  Seven rows on a sixteen byte stride. The stride is sixteen
;  rather than fourteen so a row address is a shift instead of a
;  multiply, and the two spare bytes are not waste: byte 14 is a
;  permanent zero, so every row is already a null terminated
;  string that PrintStrPx2 can print in place with no fixed
;  length variant and no copy into scratch.
;
;  Geometry is derived from the window model on every use rather
;  than kept in step by hand. A second copy of WinX that has to
;  be updated separately is exactly what CtlSync exists to avoid.
; ============================================================
NOTECOLS        equ     14              ; characters across, indices 0..13
NOTELAST        equ     NOTECOLS-1
; The document is sixteen lines and the window shows as many of them
; as it is tall. It was seven, which was however many the window
; happened to fit, so the document and the view were the same thing
; and there was nothing to scroll.
;
; Sixteen rather than a rounder number because sixteen rows on a
; sixteen byte stride is 256 bytes, which is exactly RAMCHUNK. The
; document is one file and no backend has to grow to hold it. Twenty
; four was tried first and it does not fit: a save kept the first 256
; bytes and said nothing, which is the kind of quiet that this project
; has learned to distrust.
NOTEROWS        equ     16
NOTELASTROW     equ     NOTEROWS-1
NOTESTRIDE      equ     16
NOTESIZE        equ     NOTEROWS*NOTESTRIDE     ; 384 bytes
NOTEIND         equ     1               ; text starts one column in from WinX
; The interior is 61 pixel rows and seven lines need 56, so five are
; spare. Two above and three below is the closest to balanced that an
; odd number allows, and text hard against the title bar reads as an
; overflow.
NOTETOP         equ     WinCapH+2

; ------------------------------------------------------------
;  F2: the per instance state, as one record
;  Everything a second notepad would need its own copy of lives
;  here and nowhere else, contiguous so that swapping it is one
;  LDIR. What is left below in the code is scratch: it is written
;  and read inside a single call and no window owns it.
;
;  The name is in the record rather than assembled as "NOTE",
;  because a name that is assembled is one name for every
;  instance. NoteInit writes it, which is also more honest: a
;  fresh document is called NOTE because something decided so,
;  not because the assembler happened to put it there.
; ------------------------------------------------------------
NOTENAMEMAX     equ     10              ; RamNorm refuses anything longer
NoteState       equ     TAPEBUF + TAPEMAX
NoteBuf         equ     NoteState                       ; NOTESIZE bytes
NoteName        equ     NoteBuf + NOTESIZE              ; and a terminator
NoteCX          equ     NoteName + NOTENAMEMAX + 1
NoteCY          equ     NoteCX + 1
NoteTop         equ     NoteCY + 1      ; the first document row on screen
NoteEdited      equ     NoteTop + 1
NoteDirty       equ     NoteEdited + 1
; Whether this document differs from the last thing written to or read
; from a file. NoteEdited is about the last key and NoteDirty about the
; last WinGrab; this one is about the last save, which is the only one
; of the three worth stopping a person over.
NoteModified    equ     NoteDirty + 1
NOTESTSZ        equ     NoteModified + 1 - NoteState

; NOTEREFLOW is set by anything that moved more than one line: a
; scroll, a split or a join. NoteKeyPaint repaints the whole interior
; when it is set and one line when it is not, which is the difference
; between 1,500 T and 20,000 T on an ordinary keystroke.
NOTEREFLOW      equ     2

; ------------------------------------------------------------
;  Addressing
; ------------------------------------------------------------
; A = row index. Returns HL = the start of that row's text.
NoteRowAddr:
                ld      l,a
                ld      h,0
                add     hl,hl
                add     hl,hl
                add     hl,hl
                add     hl,hl                   ; *16
                ld      de,NoteBuf
                add     hl,de
                ret

; Returns HL = the start of the cursor's row.
NoteCurAddr:
                ld      a,(NoteCY)
                jp      NoteRowAddr

; A = document row. Returns A = that line's top, in rows below the
; title bar, and
; carry set if the row is not on screen at all. Every painter checks
; the carry, so a document longer than its window cannot paint outside
; it: that is the whole of what scrolling has to be careful about.
NoteRowPx:
                ld      c,a
                ld      a,(NoteTop)
                ld      b,a
                ld      a,c
                sub     b
                ret     c                       ; above the top of the view
                push    af
                call    NoteVisRows
                ld      b,a
                pop     af
                cp      b
                ccf
                ret     c                       ; below the bottom of it
                add     a,a
                add     a,a
                add     a,a                     ; *8
                add     a,NOTETOP-WinCapH
                or      a                       ; and no carry: it is visible
                ret

; ------------------------------------------------------------
;  How much of the document the window shows
;  Derived from WinH on every use rather than kept in step by
;  hand, which is the same rule the rest of this file already
;  follows for WinX and WinY. It is also what makes a resizable
;  window cost the notepad nothing: the view is a function of the
;  window and there is no second copy to update.
; ------------------------------------------------------------
; out: A = visible text rows, at least one and at most the document
NoteVisRows:
                call    ApiWinRows
                sub     NOTETOP-WinCapH+2       ; the inset, and the gap above
                                                ; the bottom border
                jr      c,NvrOne
                srl     a                       ; /8, and SRL rather than RRA
                srl     a                       ; because RRA rotates bit 0
                srl     a                       ; back in through bit 7
                jr      z,NvrOne
                cp      NOTEROWS
                ret     c
                ld      a,NOTEROWS
                ret
NvrOne:
                ld      a,1
                ret

; ------------------------------------------------------------
;  NoteScroll
;  Brings the cursor's row into view and reports whether the view
;  moved. Called after every cursor movement, so the cursor can
;  never be somewhere the window is not showing.
; ------------------------------------------------------------
; out: Z set if nothing moved
NoteScroll:
                ld      a,(NoteCY)
                ld      c,a
                ld      a,(NoteTop)
                cp      c
                jr      z,NsSame
                jr      c,NsBelowTop
                ld      a,c                     ; the cursor is above the view
                ld      (NoteTop),a
                or      1                       ; NZ: the view moved
                ret
NsBelowTop:
                call    NoteVisRows
                ld      b,a
                ld      a,(NoteTop)
                add     a,b
                dec     a                       ; the last visible row
                cp      c
                jr      nc,NsSame
                ld      a,c                     ; the cursor is below the view
                sub     b
                inc     a
                ld      (NoteTop),a
                or      1
                ret
NsSame:
                xor     a
                ret

; Returns C = text column A, in columns from the left border.
NoteColByte:
                add     a,NOTEIND-1
                ld      c,a
                ret

; ------------------------------------------------------------
;  Painting
;  Every entry point here leaves PrintInv clear, because leaving
;  it set would silently invert whatever the next caller printed
;  and the settings panel has already been bitten by shared state
;  of exactly this kind.
; ------------------------------------------------------------
; A = document row. Paints the whole line, cursor not included. A row
; the window is not currently showing paints nothing rather than
; painting outside the window, which is what scrolling asks of every
; painter in this file.
NoteDrawLine:
                push    af
                call    NoteRowPx
                jr      c,NdlOff
                ld      b,a
                xor     a
                call    NoteColByte
                pop     af
                push    bc
                call    NoteRowAddr
                pop     bc
                jp      ApiWinPrint
NdlOff:
                pop     af
                ret

; F2's draw vector: the interior of a notepad window, which is the
; document and the cursor. WinDraw used to call the two in sequence
; itself, after an IF on the kind.
NoteDrawInterior:
                call    NoteDrawAll
                jp      NoteCurDraw

; The visible rows, and only those. Walking all twenty four and
; letting NoteDrawLine reject seventeen of them would be correct and
; would cost seventeen calls to work that out.
; ------------------------------------------------------------
;  NoteClampTop
;  The view must not sit further down than the document reaches.
;  A window that grows would otherwise keep the top it had and
;  fill the new space with blank rows under the text, which reads
;  as the document having ended when it has not.
;
;  It does not end by scrolling to the cursor, and it used to.
;  Doing that made the view a function of the cursor on every
;  single draw, so anything that moved the view on its own, which
;  since the scroll bar is a thing that happens, was undone
;  before it could be seen. The cursor pulls the view in
;  NoteScroll, which runs on a key, and that is the only place
;  it should.
; ------------------------------------------------------------
; out: A = the last row the view has any business reaching, which is
; the last row with text on it or the row the cursor is on, whichever
; is further down. The cursor sits on the blank row after the last
; line as often as not, and a view that could not reach it would fight
; whatever put it there.
NoteReach:
                ld      a,NOTELASTROW
                ld      (NoteRowIx),a
NrhFind:
                ld      a,(NoteRowIx)
                call    NoteLineLen
                or      a
                jr      nz,NrhGot
                ld      a,(NoteRowIx)
                or      a
                jr      z,NrhGot
                dec     a
                ld      (NoteRowIx),a
                jr      NrhFind
NrhGot:
                ld      a,(NoteCY)
                ld      b,a
                ld      a,(NoteRowIx)
                cp      b
                ret     nc
                ld      a,b
                ret

NoteClampTop:
                call    NoteReach
                ld      (NoteRowIx),a
                call    NoteVisRows
                ld      b,a
                ld      a,(NoteRowIx)
                inc     a                       ; the rows it uses
                sub     b
                jr      c,NctTop                ; the whole document fits
                ld      b,a
                ld      a,(NoteTop)
                cp      b
                ret     c
                ld      a,b
                ld      (NoteTop),a
                ret
NctTop:
                xor     a
                ld      (NoteTop),a
                ret

NoteDrawAll:
                call    NoteClampTop
                call    NoteVisRows
                ld      b,a
                ld      a,(NoteTop)
                ld      c,a
NdaLoop:
                push    bc
                ld      a,c
                call    NoteDrawLine
                pop     bc
                inc     c
                ld      a,c
                cp      NOTEROWS
                jr      nc,NdaDone              ; the document ran out first
                djnz    NdaLoop
                ret
NdaDone:
                ret

; Repaint the single character under the cursor, uninverted. This is
; how the cursor is lifted: there is no save under for one cell and
; there does not need to be, because the character that belongs there
; is in the buffer.
NoteCurErase:
                ld      a,(NoteCY)
                call    NoteRowPx
                ret     c                       ; scrolled out of view
                ld      b,a
                ld      a,(NoteCX)
                call    NoteColByte
                push    bc
                call    NoteCurAddr
                ld      a,(NoteCX)
                ld      e,a
                ld      d,0
                add     hl,de
                ld      a,(hl)
                ld      (NoteCell),a
                pop     bc
                ld      hl,NoteCell
                jp      ApiWinPrint

; Draw the cursor: an inverted cell reporting the shift state. No
; shift knocks out the character underneath, CAPS shows a C and
; SYMBOL SHIFT an S, which is Sinclair BASIC's idea and the answer to
; a real problem, since this keyboard hides its punctuation.
NoteCurDraw:
                ld      a,(NoteCY)
                call    NoteRowPx
                ret     c                       ; scrolled out of view
                ld      b,a
                ld      a,(NoteCX)
                call    NoteColByte
                push    bc
                call    NoteCurAddr
                ld      a,(NoteCX)
                ld      e,a
                ld      d,0
                add     hl,de
                ld      a,(hl)
                ld      (NoteCell),a
                call    ApiKbdMods
                bit     1,a
                jr      nz,NcdSym
                bit     0,a
                jr      nz,NcdCaps
                jr      NcdGo
NcdCaps:
                ld      a,'C'
                ld      (NoteCell),a
                jr      NcdGo
NcdSym:
                ld      a,'S'
                ld      (NoteCell),a
NcdGo:
                pop     bc
                ld      hl,NoteCell
                jp      ApiWinPrintInv

NoteCell:       defb    ' ',0

; ------------------------------------------------------------
;  Editing
;  Every routine here works on the buffer only. Painting is the
;  caller's business, so the bench can drive the whole editor
;  with no screen involved and still check the result.
; ------------------------------------------------------------
; F2's init vector. A fresh instance is a blank document called NOTE.
NoteInit:
                ld      hl,TxtNoteName
                call    NoteSetName
                ; fall through, because a new instance and NEW are the
                ; same thing done at different moments

; Blank the buffer and put the cursor home.
NoteNew:
                xor     a
                ld      (NoteRowIx),a
                ld      b,NOTEROWS
NnRow:
                push    bc
                ld      a,(NoteRowIx)
                call    NoteBlankRow
                ld      a,(NoteRowIx)
                inc     a
                ld      (NoteRowIx),a
                pop     bc
                djnz    NnRow
                xor     a
                ld      (NoteCX),a
                ld      (NoteCY),a
                ld      (NoteTop),a
                ld      (NoteEdited),a
                ld      (NoteDirty),a
                ld      (NoteModified),a
                ret

NoteRowIx:      defb    0

; A = character. Inserts at the cursor, pushing the rest of the line
; right, and the character falling off column thirteen is gone. The
; cursor then advances, wrapping to the next line rather than sitting
; past the end, because a cursor at column fourteen has nowhere to be
; drawn.
NoteInsert:
                ld      (NoteChar),a
                call    NoteCurAddr
                ld      (NoteLine),hl
                ld      a,NOTELAST
                ld      c,a
                ld      a,(NoteCX)
                cp      c
                jr      nc,NiPut                ; at the last column, no shift
                ; shift NOTELAST-1-CX+1 bytes right by one, from the top down
                ld      b,a
                ld      a,NOTELAST-1
                sub     b
                inc     a
                ld      c,a
                ld      b,0
                ld      hl,(NoteLine)
                ld      de,NOTELAST-1
                add     hl,de
                ld      d,h
                ld      e,l
                inc     de                      ; DE = HL + 1
                lddr
NiPut:
                ld      hl,(NoteLine)
                ld      a,(NoteCX)
                ld      e,a
                ld      d,0
                add     hl,de
                ld      a,(NoteChar)
                ld      (hl),a
                ld      a,1
                ld      (NoteEdited),a
                ld      (NoteModified),a
                ; advance
                ld      a,(NoteCX)
                inc     a
                cp      NOTECOLS
                jr      c,NiSetX
                ld      a,(NoteCY)
                inc     a
                cp      NOTEROWS
                jr      nc,NiEnd                ; last line of the document
                ld      (NoteCY),a
                xor     a
NiSetX:
                ld      (NoteCX),a
                ret
NiEnd:
                ld      a,NOTELAST
                ld      (NoteCX),a
                ret

; ------------------------------------------------------------
;  Moving whole lines
;  A split and a join both shift every line below the cursor by
;  one, in opposite directions, and both walk from the end that
;  cannot overwrite what it has not read yet: opening a row copies
;  upwards from the bottom, closing one copies downwards from the
;  top. Getting that backwards does not spoil one line, it smears
;  one line over all the rest.
;
;  The row being worked on is held in memory rather than a
;  register because both loops end in LDIR, and this project has
;  paid for that lesson twice already.
; ------------------------------------------------------------
; A = row. Blanks it, terminator and all.
NoteBlankRow:
                call    NoteRowAddr
                ld      b,NOTECOLS
                ld      a,' '
NbrLoop:
                ld      (hl),a
                inc     hl
                djnz    NbrLoop
                ld      (hl),0                  ; the permanent terminator
                inc     hl
                ld      (hl),0
                ret

; A = row. Returns A = characters up to and including the last one
; that is not a space, so a blank line is nought.
NoteLineLen:
                call    NoteRowAddr
                ld      de,NOTELAST
                add     hl,de
                ld      b,NOTECOLS
NllLoop:
                ld      a,(hl)
                cp      ' '
                jr      nz,NllGot
                dec     hl
                djnz    NllLoop
                xor     a
                ret
NllGot:
                ld      a,b
                ret

; A = the row to open up. Everything from there down moves one line
; later and the document's last line falls off the end. The opened
; row is left holding a copy and the caller blanks or fills it.
NoteOpenRow:
                ld      (NoteRowIx),a
                ld      a,NOTELASTROW
                ld      (NoteRowDst),a
NorLoop:
                ld      a,(NoteRowDst)
                ld      hl,NoteRowIx
                cp      (hl)
                ret     z                       ; arrived at the opened row
                dec     a
                call    NoteRowAddr             ; the source, one row earlier
                push    hl
                ld      a,(NoteRowDst)
                call    NoteRowAddr
                ex      de,hl                   ; DE destination, HL source
                pop     hl
                ld      bc,NOTESTRIDE
                ldir
                ld      a,(NoteRowDst)
                dec     a
                ld      (NoteRowDst),a
                jr      NorLoop

; A = the row to close up. Everything below it moves one line earlier
; and the document's last line becomes blank.
NoteCloseRow:
                ld      (NoteRowDst),a
NcrLoop:
                ld      a,(NoteRowDst)
                cp      NOTELASTROW
                jr      nc,NcrBlank
                inc     a
                call    NoteRowAddr             ; the source, one row later
                push    hl
                ld      a,(NoteRowDst)
                call    NoteRowAddr
                ex      de,hl
                pop     hl
                ld      bc,NOTESTRIDE
                ldir
                ld      a,(NoteRowDst)
                inc     a
                ld      (NoteRowDst),a
                jr      NcrLoop
NcrBlank:
                ld      a,NOTELASTROW
                jp      NoteBlankRow

NoteRowDst:     defb    0
NoteJoinAt:     defb    0

; Backspace. At column nought it joins this line onto the end of the
; previous one, which is what it could not do while the document was
; the same seven lines as the view.
NoteBack:
                ld      a,(NoteCX)
                or      a
                jr      z,NbEdge
                dec     a
                ld      (NoteCX),a
                ld      c,a
                call    NoteCurAddr
                ld      e,c
                ld      d,0
                add     hl,de
                ld      d,h
                ld      e,l
                inc     hl                      ; HL = DE + 1
                ld      a,NOTELAST
                sub     c
                ld      c,a
                ld      b,0
                or      a
                jr      z,NbTail
                ldir
NbTail:
                call    NoteCurAddr
                ld      de,NOTELAST
                add     hl,de
                ld      (hl),' '
                ld      a,1
                ld      (NoteEdited),a
                ret
; The join. It happens only when the whole of this line fits on the
; end of the previous one. A join that dropped the overflow would be
; a backspace that silently deletes a line, so when it does not fit
; the cursor moves and the text stays where it is.
NbEdge:
                ld      a,(NoteCY)
                or      a
                ret     z
                dec     a
                call    NoteLineLen
                ld      (NoteJoinAt),a
                ld      a,(NoteCY)
                call    NoteLineLen
                ld      c,a                     ; what this line carries
                ld      a,(NoteJoinAt)
                add     a,c
                cp      NOTECOLS+1
                jr      nc,NbMoveOnly
                ld      a,c
                or      a
                jr      z,NbJoinEmpty           ; nothing to carry over
                ld      b,0
                ld      a,(NoteCY)
                call    NoteRowAddr
                push    hl                      ; this line, from column nought
                ld      a,(NoteCY)
                dec     a
                call    NoteRowAddr
                ld      a,(NoteJoinAt)
                ld      e,a
                ld      d,0
                add     hl,de
                ex      de,hl                   ; DE = where it lands
                pop     hl
                ldir
NbJoinEmpty:
                ld      a,(NoteCY)
                call    NoteCloseRow
                ld      hl,NoteCY
                dec     (hl)
                ld      a,(NoteJoinAt)
                ld      (NoteCX),a
                ld      a,NOTEREFLOW
                ld      (NoteEdited),a
                ld      a,1
                ld      (NoteModified),a
                ret
NbMoveOnly:
                ld      hl,NoteCY
                dec     (hl)
                ld      a,NOTELAST
                ld      (NoteCX),a
                ret

; ENTER splits the line at the cursor. What was to the right of it
; becomes the next line and everything below moves down. It used to
; move down and home without carrying anything, because with the
; document and the view the same seven lines there was nowhere for
; the rest of the document to go.
NoteEnter:
                ld      a,(NoteCY)
                cp      NOTELASTROW
                ret     nc                      ; no row below to open
                inc     a
                call    NoteOpenRow
                ld      a,(NoteCY)
                inc     a
                call    NoteBlankRow
                ld      a,NOTECOLS
                ld      c,a
                ld      a,(NoteCX)
                neg
                add     a,c                     ; how much is to the right
                ld      (NoteRowDst),a
                or      a
                jr      z,NeNoTail
                ld      c,a
                ld      b,0
                ld      a,(NoteCY)
                call    NoteRowAddr
                ld      a,(NoteCX)
                ld      e,a
                ld      d,0
                add     hl,de
                push    hl                      ; the tail
                ld      a,(NoteCY)
                inc     a
                call    NoteRowAddr
                ex      de,hl                   ; DE = the new line
                pop     hl
                ldir
                ; and where it was becomes spaces
                ld      a,(NoteCY)
                call    NoteRowAddr
                ld      a,(NoteCX)
                ld      e,a
                ld      d,0
                add     hl,de
                ld      a,(NoteRowDst)
                ld      b,a
                ld      a,' '
NeBlank:
                ld      (hl),a
                inc     hl
                djnz    NeBlank
NeNoTail:
                ld      hl,NoteCY
                inc     (hl)
                xor     a
                ld      (NoteCX),a
                ld      a,NOTEREFLOW
                ld      (NoteEdited),a
                ld      a,1
                ld      (NoteModified),a
                ret

NoteLeft:
                ld      a,(NoteCX)
                or      a
                ret     z
                dec     a
                ld      (NoteCX),a
                ret
NoteRight:
                ld      a,(NoteCX)
                cp      NOTELAST
                ret     nc
                inc     a
                ld      (NoteCX),a
                ret
NoteUp:
                ld      a,(NoteCY)
                or      a
                ret     z
                dec     a
                ld      (NoteCY),a
                ret
NoteDown:
                ld      a,(NoteCY)
                cp      NOTELASTROW
                ret     nc
                inc     a
                ld      (NoteCY),a
                ret

; ------------------------------------------------------------
;  The key handler
;  A = key code. Returns with the screen already updated, so the
;  event handler has nothing to do afterwards.
; ------------------------------------------------------------
NoteKey:
                ld      (NoteChar),a
                xor     a
                ld      (NoteEdited),a
                ld      a,(NoteChar)
                cp      KEY_LEFT
                jr      nz,NkNotL
                call    NoteLeft
                jr      NkView
NkNotL:
                cp      KEY_RIGHT
                jr      nz,NkNotR
                call    NoteRight
                jr      NkView
NkNotR:
                cp      KEY_UP
                jr      nz,NkNotU
                call    NoteUp
                jr      NkView
NkNotU:
                cp      KEY_DOWN
                jr      nz,NkNotD
                call    NoteDown
                jr      NkView
NkNotD:
                cp      KEY_DELETE
                jr      nz,NkNotBs
                call    NoteBack
                jr      NkView
NkNotBs:
                cp      KEY_ENTER
                jr      nz,NkNotEnt
                call    NoteEnter
                jr      NkView
NkNotEnt:
                cp      ' '
                ret     c                       ; below space, not ours
                cp      128
                ret     nc
                call    NoteInsert
                ; and then every one of them ends here, because a cursor
                ; the window is not showing is the one thing a scrolling
                ; editor must never leave behind
NkView:
                call    NoteScroll
                ret     z
                ld      a,NOTEREFLOW            ; the view moved, so one line
                ld      (NoteEdited),a          ; is not what has to be painted
                ret

; The screen half, kept apart so the bench can drive NoteKey with no
; display at all and still check the buffer.
;
; The line repainted is the one the cursor was on when the key
; arrived, not the one it ends on. Insert at the last column wraps the
; cursor to the next line, and it is the line the character landed on
; that changed.
;
; NoteDirty is set whatever the key did, because the cursor is painted
; into the window too, so even a move that changes no text makes the
; grabbed buffer stale. The cost of being generous here is one WinGrab
; per drag rather than per key, which is the whole point of deferring.
NoteKeyPaint:
                ld      (NoteChar),a
                call    NoteCurErase
                ld      a,(NoteCY)
                ld      (NoteOldY),a
                ld      a,(NoteChar)
                call    NoteKey
                ld      a,(NoteEdited)
                cp      NOTEREFLOW
                jr      z,NkpAll                ; a scroll, a split or a join
                or      a
                jr      z,NkpNoText
                ld      a,(NoteOldY)
                call    NoteDrawLine
                jr      NkpNoText
NkpAll:
                ; The interior is cleared first. A join and a scroll both
                ; leave fewer lines than were there, and painting the new
                ; ones over the old leaves the difference on screen.
                call    ApiWinClear
                call    NoteDrawAll
NkpNoText:
                ld      a,1
                ld      (NoteDirty),a
                call    NoteCurDraw
                ; The bar is chrome and WinDraw normally paints it, but this
                ; repaints the interior without going through WinDraw and
                ; the clear has just wiped it. An application that
                ; repaints its own interior owes the frame its chrome back.
                jp      ApiWndBar

; ------------------------------------------------------------
;  Keeping the window buffer honest
;  The buffer is what a drag blits, so text typed after the last
;  WinGrab would vanish the moment the window moved.
;
;  Re-grabbing on every keystroke would be wasteful, because the
;  buffer is only ever read at the start of a drag. How wasteful
;  is not measured: RectGrab has no bench subject. BlitRect, which
;  walks the same rectangle the other way, costs 26,048 T, so the
;  grab is expected to be near that, but expected is not measured
;  and this comment should not be quoted as if it were.
;
;  Deferring it to the moment a drag starts costs nothing either
;  way and removes the question.
; ------------------------------------------------------------
NoteSync:
                ld      a,(NoteDirty)
                or      a
                ret     z
                xor     a
                ld      (NoteDirty),a
                jp      ApiWinGrab

; ------------------------------------------------------------
;  Storage
;  The whole buffer is one blob. There is no header and no magic:
;  the note is text, and the checks that matter are the two that
;  stop a bad file hurting the printer.
; ------------------------------------------------------------
; NoteSave and NoteLoad work on NoteName. G3 needs the same two
; against a name the user chose, so the name is now a parameter and
; the old entry points pass NoteName in. The chosen name is copied
; into NoteName as well, because after a save as, the note is that
; file, and a later SAVE from the FILE menu has to agree.
NoteSave:
                ld      hl,NoteName
NoteSaveAs:
                call    NoteSetName
                ld      hl,NoteName
                ld      b,FA_OVERWRITE
                call    ApiStOpen
                ret     c
                ld      (NoteHandle),a
                ld      hl,NoteBuf
                ld      bc,NOTESIZE
                call    ApiStWrite
                ld      (NoteGot),bc
                ld      a,(NoteHandle)
                call    ApiStClose
                ld      hl,(NoteGot)
                ld      de,NOTESIZE
                or      a
                sbc     hl,de
                jr      nz,NsShort
                xor     a                       ; written, so there is nothing
                ld      (NoteModified),a        ; left to warn anybody about
                or      a
                ret
NsShort:
                scf
                ret

NoteLoad:
                ld      hl,NoteName
NoteLoadAs:
                call    NoteSetName
                ld      hl,NoteName
                ld      b,FA_READ
                call    ApiStOpen
                jr      c,NlFail
                ld      (NoteHandle),a
                ld      hl,NoteBuf
                ld      bc,NOTESIZE
                call    ApiStRead
                ld      (NoteGot),bc
                ld      a,(NoteHandle)
                call    ApiStClose
                ld      hl,(NoteGot)
                ld      de,NOTESIZE
                or      a
                sbc     hl,de
                jr      nz,NlFail
                call    NoteSanitise
                xor     a
                ld      (NoteCX),a
                ld      (NoteCY),a
                ld      (NoteTop),a
                ld      (NoteModified),a        ; this is the file, unchanged
                ld      a,1
                ld      (NoteDirty),a
                or      a
                ret
NlFail:
                call    NoteNew
                scf
                ret

; HL = a name, copied into NoteName. Passing NoteName itself copies
; it onto itself a byte at a time, which is correct and costs about
; forty T states, so there is no case to test for.
NoteSetName:
                ld      de,NoteName
                ld      b,NOTENAMEMAX
NsnLoop:
                ld      a,(hl)
                ld      (de),a
                or      a
                ret     z
                inc     hl
                inc     de
                djnz    NsnLoop
                xor     a
                ld      (de),a                  ; a long name is truncated
                ret                             ; rather than run off the end

; Two things have to be true before an arbitrary file is printed.
; Byte 14 of every row must be zero, or PrintStrPx2 walks off the end
; of the buffer looking for a terminator. And every text byte must be
; printable, because a glyph is read from CHARSET + c*8 with CHARSET
; at $3C00, so a byte of 128 or more fetches from $4000 upwards and
; renders screen memory as letterforms. Neither is a crash. Both look
; like a mystery later.
NoteSanitise:
                ld      b,NOTEROWS
                xor     a
                ld      (NoteRowIx),a
NsanRow:
                push    bc
                ld      a,(NoteRowIx)
                call    NoteRowAddr
                ld      b,NOTECOLS
NsanCol:
                ld      a,(hl)
                cp      ' '
                jr      c,NsanBad
                cp      128
                jr      c,NsanOk
NsanBad:
                ld      (hl),' '
NsanOk:
                inc     hl
                djnz    NsanCol
                ld      (hl),0
                inc     hl
                ld      (hl),0
                ld      a,(NoteRowIx)
                inc     a
                ld      (NoteRowIx),a
                pop     bc
                djnz    NsanRow
                ret

; ------------------------------------------------------------
;  Menu actions
;  NoteMenuOpen used to be here and was dead from G3, when the
;  FILE menu started pointing OPEN at the file panel instead. It
;  was still being assembled, which is how dead code survives in
;  a project that checks for it: nothing referenced it and
;  nothing said so.
; ------------------------------------------------------------
; F2 changed what NEW means. In a system with one document it blanked
; the one there was, which is the only thing it could mean and also a
; menu item that silently destroys what you typed. In a system that
; can hold four windows it opens another notepad, and blanking is what
; you get by opening one and closing the other.
NoteMenuNew:
                ld      a,APP_NOTE
                jp      ApiWndOpen

; ------------------------------------------------------------
;  NoteScrollInfo
;  The scroll model, which is three numbers and no drawing. A
;  unit is a line of the document.
; ------------------------------------------------------------
NoteScrollInfo:
                call    NoteReach
                inc     a
                ld      b,a                     ; as far as the view can go,
                                                ; not as far as the document
                                                ; could hold. A bar whose
                                                ; thumb implies room that is
                                                ; not there is a bar that
                                                ; stops moving and looks
                                                ; broken.
                call    NoteVisRows
                ld      c,a
                ld      a,(NoteTop)
                ret

; ------------------------------------------------------------
;  NoteScrollTo
;  in: A = the document row to put at the top of the view
;  The cursor comes with it rather than being left behind
;  off screen. An editor with a caret somewhere you cannot see is
;  a thing that will surprise you the next time you type, and
;  NoteScroll would drag the view back to it anyway on the very
;  next key.
; ------------------------------------------------------------
NoteScrollTo:
                ; The wanted row goes in memory, not in B. NoteReach loads
                ; NoteCY into B on its way to comparing it, so holding the
                ; argument there meant every scroll asked for wherever the
                ; cursor happened to be and the view never moved at all.
                ld      (NoteWantTop),a
                call    NoteVisRows
                ld      c,a                     ; C = showing
                call    NoteReach
                inc     a
                sub     c                       ; the furthest top there is,
                jr      nc,NstHave              ; and it is the same limit
                xor     a                       ; NoteClampTop uses, because
NstHave:                                        ; two clamps disagreeing is
                ld      b,a                     ; how the bar moved the view
                ld      a,(NoteWantTop)         ; and the draw put it back
                cp      b
                jr      c,NstTop
                ld      a,b
NstTop:
                ld      (NoteTop),a
                ; The cursor stays where it is. Dragging it along made the
                ; range shrink as you scrolled, because the reach depends
                ; on where the cursor is: scrolling up moved the cursor up
                ; and then the bottom was no longer reachable.
NstPaint:
                call    ApiPtrRestore
                call    ApiWinClear
                call    NoteDrawAll
                call    NoteCurDraw
                call    ApiWndBar
                call    ApiWinGrab
                call    ApiPtrSaveBg
                jp      ApiPtrDraw

; ------------------------------------------------------------
;  NotePrint
;  A save writes NOTESIZE bytes, which is the document including
;  its sixteen byte stride and the terminators inside it. That is
;  the right thing to write to a file and the wrong thing to send
;  to a printer, which would print the padding as well.
;
;  So printing walks the rows, sends each line's text up to its
;  last non space character, and follows it with ENTER. That is a
;  printed document rather than a memory dump, and it stops at
;  the last row that has anything on it, because sixteen lines of
;  which nine are blank is nine lines of wasted paper on a roll
;  four inches wide.
;
;  It goes through the storage layer like everything else, so the
;  printer being a device rather than a special case is not a
;  claim made in a comment: this routine cannot tell.
; ------------------------------------------------------------
NotePrint:
                call    NoteIsFront
                ret     nz
                call    ApiStIdent              ; put back whatever was
                ld      (NotePrtWas),a          ; selected, the same way the
                ld      a,ST_PRINT              ; commander does
                call    ApiStSelect
                ret     c
                ld      hl,NoteName
                ld      b,FA_OVERWRITE
                call    ApiStOpen
                jr      c,NotePrtFail
                ld      (NoteHandle),a
                ; the last row with anything on it
                ld      a,NOTELASTROW
                ld      (NoteRowIx),a
NprLast:
                ld      a,(NoteRowIx)
                call    NoteLineLen
                or      a
                jr      nz,NprGotLast
                ld      a,(NoteRowIx)
                or      a
                jr      z,NprGotLast
                dec     a
                ld      (NoteRowIx),a
                jr      NprLast
NprGotLast:
                ld      a,(NoteRowIx)
                ld      (NotePrtLast),a
                xor     a
                ld      (NoteRowIx),a
NprRow:
                ld      a,(NoteRowIx)
                call    NoteLineLen
                or      a
                jr      z,NprNoText             ; a blank line in the middle
                                                ; is still a line
                ld      c,a
                ld      b,0
                ld      a,(NoteRowIx)
                call    NoteRowAddr
                ld      a,(NoteHandle)          ; the handle goes in A and
                call    ApiStWrite                 ; everything between the open
                                                ; and here destroys it
NprNoText:
                ld      a,(NoteRowIx)
                ld      b,a
                ld      a,(NotePrtLast)
                cp      b
                jr      z,NprDone               ; The last line needs no ENTER
                                                ; of its own: the close flushes
                                                ; the buffer, and sending one
                                                ; leaves an empty buffer for
                                                ; the close to print as a
                                                ; blank line of paper.
                ld      hl,NotePrtEol
                ld      bc,1
                ld      a,(NoteHandle)
                call    ApiStWrite
                ld      hl,NoteRowIx
                inc     (hl)
                jr      NprRow
NprDone:
                ld      a,(NoteHandle)
                call    ApiStClose
NotePrtFail:
                ld      a,(NotePrtWas)
                jp      ApiStSelect

NotePrtEol:     defb    KEY_ENTER
NoteWantTop:    defb    0
NotePrtWas:     defb    0
NotePrtLast:    defb    0

; ------------------------------------------------------------
;  NoteClose
;  F2's close vector, and the reason that vector exists.
;
;  A document with unsaved work asks before it goes, and asking
;  cannot block: the panel system is not a modal loop, it is a
;  panel that takes every press until it closes and then calls
;  back. So this refuses the close, and the answer performs it.
;  Carry set means "not yet" rather than "no".
; ------------------------------------------------------------
NoteClose:
                ld      a,(NoteModified)
                or      a
                ret     z                       ; nothing to lose
                ld      hl,NoteName             ; which document, by name,
                ld      de,TxtNoteLose          ; because two notepads can be
                ld      bc,NoteCloseAnswer      ; open and only one is going
                call    ApiDlgSave3
                scf
                ret

; A = 0 cancel, 1 discard, 2 save. The window is still the live one:
; a panel does not change which window is selected, it sits over it.
NoteCloseAnswer:
                or      a
                ret     z                       ; stay open, unchanged
                cp      2
                jr      z,NcaSave
NcaGo:
                jp      ApiWndCloseNow
NcaSave:
                call    NoteSave
                jr      nc,NcaGo
                ; The save failed, so the window stays: closing now would
                ; throw the work away having just been told to keep it.
                ld      hl,TxtNoteNoSave
                ld      de,0
                jp      ApiDlgAlert

TxtNoteLose:    defb    "UNSAVED WORK",0   ; DGW is 14 columns
TxtNoteNoSave:  defb    "COULD NOT SAVE",0

; F2. Everything in the FILE menu except DELETE acts on the document
; in front, so it has to ask whether the thing in front has one. The
; test is the descriptor the window already carries, so there is no
; second copy of "which application is this" to go stale.
NoteIsFront:
                call    ApiWinApp
                ld      de,AppNote
                or      a
                sbc     hl,de
                ret
NoteMenuSave:
                call    NoteIsFront
                ret     nz
                call    NoteSave
                ld      a,0
                jr      nc,NmsOk
                ld      a,1
NmsOk:
                ld      (NoteResult),a
                ret

NoteRefresh:
                call    ApiPtrRestore
                call    NoteDrawAll
                call    NoteCurDraw
                call    ApiWinGrab
                xor     a
                ld      (NoteDirty),a
                call    ApiPtrSaveBg
                jp      ApiPtrDraw

; Scratch, all of it. Each is written and read inside one call, so a
; second notepad shares these the way it shares the registers.
NoteHandle:     defb    0
NoteGot:        defw    0
NoteResult:     defb    0
NoteOldY:       defb    0
NoteChar:       defb    0
NoteLine:       defw    0

TxtNoteName:    defb    "NOTE",0
