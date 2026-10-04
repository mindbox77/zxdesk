; ============================================================
;  Tape backend
;  An unmodified 48K does have storage. It is tape, and it is
;  what ZX Desk has to work with before any interface is fitted.
;  This is the baseline backend, not a consolation prize.
;
;  Tape is a stream, so it cannot pretend to be a filesystem.
;  A write buffers into RAM and the close does the actual SAVE;
;  an open for reading does the LOAD and the reads then come out
;  of the buffer. That maps cleanly onto settings and small
;  documents, which is what a 48K is going to hold anyway, and
;  the capability bits say so: WRITE and PERSIST, no RANDOM and
;  no DIR.
;
;  ROM entry points, confirmed against the ROM disassembly:
;    SA_BYTES  $04C2  A = type, IX = start, DE = length
;    LD_BYTES  $0556  A = type, IX = start, DE = length,
;                     carry set to load, returns carry set on OK
;  Type is $00 for a header and $FF for data. Both are byte for
;  byte identical in 48.rom and in the 128K's ROM 1, which are not
;  the same ROM; printer.inc says where they do differ.
;
;  Two things the ROM assumes and will not tell you about:
;  IY must be $5C3A, because the ROM indexes the system
;  variables through it, and interrupts must be off, because the
;  bit timing is done by counting T states. Our IM2 handler and
;  frame counter stop for the duration, which is expected.
; ============================================================
SA_BYTES        equ     $04C2
LD_BYTES        equ     $0556
SYSVARS         equ     $5C3A

TAPEHDR         equ     17

TapeInit:
                ld      hl,0
                ld      (TapeLen),hl
                ld      (TapePos),hl
                xor     a
                ld      (TapeDirty),a
                ld      (TapeFound),a
                ret

; HL = name, B = mode. One file at a time, handle is always 1.
TapeOpen:
                ld      a,b
                ld      (TapeMode),a
                ld      a,(hl)                  ; the empty name means the
                ld      (TapeAny),a             ; next file, whatever it is
                or      a
                jr      z,TapoFresh
                ld      a,b
                cp      FA_READ
                jr      nz,TapoFresh
                ; Asked by name for the file the buffer already holds:
                ; hand it over again rather than go back to the tape.
                push    hl
                ld      de,TapeFound
TapoHeld:
                ld      a,(de)
                cp      (hl)
                jr      nz,TapoNotHeld
                inc     hl
                inc     de
                or      a
                jr      nz,TapoHeld
                pop     hl
                ld      hl,0
                ld      (TapePos),hl
                jr      TapoOk
TapoNotHeld:
                pop     hl
TapoFresh:
                call    TapeName                ; pad the name into the header
                call    TapeInit
                ld      a,(TapeMode)
                cp      FA_OVERWRITE
                jr      z,TapoWrite
                call    TapeLoad
                ret     c
                ld      hl,(TapeLen)
                ld      a,h
                or      l
                jr      nz,TapoOk
                ld      a,STERR_NOTFOUND
                scf
                ret
TapoWrite:
                ld      a,1
                ld      (TapeDirty),a
TapoOk:
                ld      a,1
                or      a
                ret

; Copy the caller's name into the header, space padded to ten
; characters the way a Spectrum header wants it.
TapeName:
                push    hl
                ld      hl,TapeHdr+1
                ld      b,10
                ld      a,' '
TapnPad:
                ld      (hl),a
                inc     hl
                djnz    TapnPad
                pop     hl
                ld      de,TapeHdr+1
                ld      b,10
TapnCopy:
                ld      a,(hl)
                or      a
                ret     z
                ld      (de),a
                inc     hl
                inc     de
                djnz    TapnCopy
                ret

; Read headers until one matches the wanted name, then pull its
; data block into the buffer. Carry set means the tape ran out or
; the block was corrupt.
TapeLoad:
IFNDEF BENCH
                ; The ROM waits on a silent tape for ever, with the desktop
                ; stopped. Say how to end it before it starts.
                ld      hl,TxtTapeWait
                ld      b,STATROW
                ld      c,0
                call    PrintStr
ENDIF
                ld      iy,SYSVARS              ; the ROM indexes through IY
                di
                ld      a,TAPETRIES
                ld      (TapeTries),a
TaplSeek:
                ld      hl,TapeTries
                dec     (hl)
                jr      z,TaplFail
                ld      ix,TapeIn
                ld      de,TAPEHDR
                xor     a                       ; $00: a header block
                scf                             ; load rather than verify
                call    LD_BYTES
                jr      c,TaplHdr
                ld      a,$7F                   ; not a header. SPACE gives
                in      a,($FE)                 ; up; anything else was a
                rra                             ; block to pass over
                jr      nc,TaplFail
                jr      TaplSeek
TaplHdr:
                ld      a,(TapeAny)
                or      a
                jr      nz,TaplNamed
                ld      a,(TapeIn)
                cp      3
                jr      nz,TaplSeek             ; only CODE files are ours
                ld      hl,(TapeIn+11)
                ld      de,TAPEMAX
                or      a
                sbc     hl,de
                jr      nc,TaplSeek             ; too long to hold: pass it
                jr      TaplTake
TaplNamed:
                ld      hl,TapeHdr+1
                ld      de,TapeIn+1
                ld      b,10
TaplCmp:
                ld      a,(de)
                cp      (hl)
                jr      nz,TaplSeek             ; not ours, keep looking
                inc     hl
                inc     de
                djnz    TaplCmp
                ld      hl,(TapeIn+11)          ; length from the header
                ld      de,TAPEMAX
                or      a
                sbc     hl,de
                jr      nc,TaplFail             ; longer than the buffer
TaplTake:
                ld      hl,(TapeIn+11)
                ld      (TapeLen),hl
                ex      de,hl
                ld      ix,TAPEBUF
                ld      a,$FF                   ; $FF: a data block
                scf
                call    LD_BYTES
                jr      nc,TaplFail
                ; the name it had, without the padding, for TapeDirEnt
                ld      hl,TapeIn+1
                ld      de,TapeFound
                ld      bc,10
                ldir
                ex      de,hl
                ld      b,9
TaplTrim:
                ld      (hl),0
                dec     hl
                ld      a,(hl)
                cp      ' '
                jr      nz,TaplOk
                djnz    TaplTrim
TaplOk:
                ei
                or      a
                ret
TaplFail:
                ei
                ld      a,STERR_NOTFOUND
                scf
                ret

; A = handle, HL = buffer, BC = count. Returns BC = bytes moved.
TapeRead:
                call    TapeAvail               ; BC = min(count, len-pos)
                ld      a,b
                or      c
                ret     z
                push    bc
                ex      de,hl                   ; DE = caller's buffer
                ld      hl,(TapePos)
                ld      bc,TAPEBUF
                add     hl,bc
                pop     bc
                push    bc
                ldir
                pop     bc
                ld      hl,(TapePos)
                add     hl,bc
                ld      (TapePos),hl
                or      a
                ret

; A = handle, HL = buffer, BC = count. Returns BC = bytes accepted.
TapeWrite:
                call    TapeRoom                ; BC = min(count, max-len)
                ld      a,b
                or      c
                ret     z
                push    bc
                ld      de,(TapeLen)
                push    hl
                ld      hl,TAPEBUF
                add     hl,de
                ex      de,hl                   ; DE = where it goes
                pop     hl                      ; HL = where it comes from
                pop     bc
                push    bc
                ldir
                pop     bc
                ld      hl,(TapeLen)
                add     hl,bc
                ld      (TapeLen),hl
                or      a
                ret

; BC = min(BC, TapeLen - TapePos)
TapeAvail:
                push    hl
                ld      hl,(TapeLen)
                ld      de,(TapePos)
                or      a
                sbc     hl,de
                jr      TapeClamp

; BC = min(BC, TAPEMAX - TapeLen)
TapeRoom:
                push    hl
                ld      hl,TAPEMAX
                ld      de,(TapeLen)
                or      a
                sbc     hl,de
TapeClamp:
                ; HL = what is available, BC = what was asked for
                push    hl
                or      a
                sbc     hl,bc
                pop     hl
                jr      c,TapcUse               ; available < asked, use it
                pop     hl
                ret                             ; asked for fits, keep BC
TapcUse:
                ld      b,h
                ld      c,l
                pop     hl
                ret

; Closing a file opened for writing is what actually saves it.
TapeClose:
                ld      a,(TapeDirty)
                or      a
                ret     z
                xor     a
                ld      (TapeDirty),a
                ld      a,3                     ; header type 3, CODE
                ld      (TapeHdr),a
                ld      hl,(TapeLen)
                ld      (TapeHdr+11),hl
                ld      hl,TAPEBUF
                ld      (TapeHdr+13),hl         ; start address
                ld      hl,$8000
                ld      (TapeHdr+15),hl         ; unused for CODE
                ld      iy,SYSVARS
                di
                ld      ix,TapeHdr
                ld      de,TAPEHDR
                xor     a                       ; header block
                call    SA_BYTES
                ld      ix,TAPEBUF
                ld      de,(TapeLen)
                ld      a,$FF                   ; data block
                call    SA_BYTES
                ei
                or      a
                ret

; A = n. The one name a tape can give: the file it last read.
TapeDirEnt:
                or      a
                jp      nz,StNoDir
                ld      a,(TapeFound)
                or      a
                jp      z,StNoDir
                ld      de,(TapeLen)
                ld      hl,TapeFound
                ret

; Blocks to pass before giving up. The bench wants the answer at
; once: its tape has nothing after the program and a silent tape
; is waited on for ever.
IFDEF BENCH
TAPETRIES       equ     2
ELSE
TAPETRIES       equ     32
ENDIF

IFNDEF BENCH
TxtTapeWait:    defb    "READING TAPE. SPACE TO STOP.    ",0
ENDIF
TapeAny:        defb    0
TapeTries:      defb    0
TapeFound:      defs    11
TapeMode:       defb    0
TapeLen:        defw    0
TapePos:        defw    0
TapeDirty:      defb    0
TapeHdr:        defs    TAPEHDR
TapeIn:         defs    TAPEHDR
