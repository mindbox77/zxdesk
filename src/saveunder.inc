; ------------------------------------------------------------
;  B3: transient surfaces, as a stack
;  A pull down menu or a dialogue has to appear over whatever is
;  already on the desktop and then vanish without forcing a full
;  repaint. The pointer already does this for its eleven rows;
;  this is the same idea for an arbitrary rectangle, built on the
;  rectangle grab and blit rather than as a second copy of them.
;
;  It used to be one buffer, and the earlier version of this
;  comment said a stack would be "a change to the sizing, not to
;  the mechanism". That was optimistic. Variable sized entries
;  cannot be walked backwards from a top pointer alone, so the
;  stack is two things: an arena the pixels are packed into, and
;  a small stack of where each entry started.
;
;  One buffer had decided three separate things, which is why
;  this was worth doing rather than living with:
;
;    the settings panel had to borrow the menu's buffer, so a
;    menu had to be closed before a panel could open;
;
;    a dialogue over a panel was impossible, so the file manager
;    could not delete anything, because delete needs a confirm;
;
;    and the buffer's twelve column width capped a filename at
;    nine characters when a tape name is ten.
;
;  The third one is fixed here by widening as well as stacking,
;  which is nearly free now the arena is in the largest block of
;  RAM on the machine rather than sharing with the window.
; ------------------------------------------------------------
SUMAXW          equ     16              ; byte columns, was 12
SUMAXH          equ     96              ; pixel rows
SUDEPTH         equ     4               ; menu, panel, confirm, and one spare
SUHDR           equ     4               ; x, y, w, h ahead of each surface
; Four full surfaces and their four headers. The headers were not
; counted once, and the fourth full push put its last sixteen bytes
; over the start of the heap.
SUARENA         equ     SUDEPTH*(SUHDR+SUMAXW*SUMAXH)   ; 6160 bytes

; The arena follows the notepad's buffer, at the top of the chain
; that starts at BUFBASE. Sizing it for four full sized surfaces is
; deliberately not clever: it costs 6K of an 11K free block, and an
; arena that can overflow is an arena that needs a failure path on
; every caller.
; F2 made the notepad's state a record rather than a bare buffer, so
; this follows the record and not the buffer inside it. Following the
; buffer put the arena on top of the name and the cursor, and the
; first thing that showed was a file panel seeded with screen pixels.
SuArena         equ     NoteState + NOTESTSZ

; F1 put the heap where the second window buffer used to be. Both
; windows draw their pixels from it now, sized to the window rather
; than to the largest window there could be.

; ------------------------------------------------------------
;  SaveUnder
;  Grabs the rectangle in SuX, SuY, SuW, SuH and pushes it.
;  Carry set and nothing pushed if the stack is full or the
;  rectangle is bigger than a surface, so a caller that ignores
;  the carry gets a missing surface rather than a corrupted one.
; ------------------------------------------------------------
SaveUnder:
                ld      a,(SuDepth)
                cp      SUDEPTH
                jr      nc,SuFull
                ld      a,(SuW)
                or      a
                jr      z,SuFull                ; nothing to save is a failure,
                cp      SUMAXW+1                ; not a silent success
                jr      nc,SuFull
                ld      a,(SuH)
                or      a
                jr      z,SuFull
                cp      SUMAXH+1
                jr      nc,SuFull

                ; remember where this entry starts, so the pop can find it
                ld      hl,(SuTop)
                push    hl
                ld      a,(SuDepth)
                call    SuSlot
                pop     de
                ld      (hl),e
                inc     hl
                ld      (hl),d
                ex      de,hl                   ; HL = the entry

                ; the header, so a pop knows where to put the pixels back
                ; without trusting SuX and SuY to be unchanged. They will
                ; not be: the panel that pushed may have opened another.
                ld      a,(SuX)
                ld      (hl),a
                inc     hl
                ld      a,(SuY)
                ld      (hl),a
                inc     hl
                ld      a,(SuW)
                ld      (hl),a
                inc     hl
                ld      a,(SuH)
                ld      (hl),a
                inc     hl
                ld      (RcBuf),hl

                ld      a,(SuX)
                ld      (RcX),a
                ld      a,(SuY)
                ld      (RcY),a
                ld      a,(SuW)
                ld      (RcW),a
                ld      a,(SuH)
                ld      (RcH),a
                call    RectGrab

                ; advance the arena pointer past header and pixels
                ld      hl,(SuTop)
                ld      de,SUENTHDR
                add     hl,de
                ld      a,(SuH)
                ld      b,a
                ld      a,(SuW)
                ld      e,a
                ld      d,0
SuAdv:
                add     hl,de
                djnz    SuAdv
                ld      (SuTop),hl
                ld      hl,SuDepth
                inc     (hl)
                or      a
                ret
SuFull:
                scf
                ret

; ------------------------------------------------------------
;  RestoreUnder
;  Pops the topmost surface and paints it back. Carry set if
;  there was nothing to pop, which is not an error: a close
;  routine that runs twice should be harmless.
; ------------------------------------------------------------
RestoreUnder:
                ld      a,(SuDepth)
                or      a
                jr      z,SuEmpty
                dec     a
                ld      (SuDepth),a
                call    SuSlot
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ex      de,hl                   ; HL = the entry
                ld      (SuTop),hl              ; the arena unwinds to here

                ld      a,(hl)
                ld      (RcX),a
                inc     hl
                ld      a,(hl)
                ld      (RcY),a
                inc     hl
                ld      a,(hl)
                ld      (RcW),a
                inc     hl
                ld      a,(hl)
                ld      (RcH),a
                inc     hl
                ld      (RcBuf),hl
                call    RectBlit
                or      a
                ret
SuEmpty:
                scf
                ret

; A = a stack slot. Returns HL pointing at its two byte entry.
SuSlot:
                add     a,a
                ld      l,a
                ld      h,0
                ld      de,SuStack
                add     hl,de
                ret

SUENTHDR        equ     4               ; x, y, w, h

SuX:            defb    0
SuY:            defb    0
SuW:            defb    0
SuH:            defb    0
SuDepth:        defb    0
SuTop:          defw    SuArena
SuStack:        defs    SUDEPTH*2

RcX:            defb    0
RcY:            defb    0
RcW:            defb    0
RcH:            defb    0
RcBuf:          defw    0
