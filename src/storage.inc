; ============================================================
;  E1: storage device layer
;  Five entry points behind a vector table, so nothing above this
;  layer knows which backend it is talking to. Same bet as the
;  drawing device layer, and the reason the Next port should be a
;  backend swap rather than a rewrite.
;
;    StOpen   HL = name, B = mode   -> A = handle, carry on error
;    StClose  A = handle
;    StRead   A = handle, HL = buffer, BC = count -> BC = read
;    StWrite  A = handle, HL = buffer, BC = count -> BC = written
;    StIdent                        -> A = backend id
;
;  Names are up to eight characters, null terminated. Modes match
;  esxDOS so the eventual backend needs no translation.
;
;  The backend is selected explicitly, not probed. RST $08 on a
;  machine with no DivMMC runs the ROM's error restart and takes
;  the program with it, so there is no safe way to ask.
;
;  Dispatch is the jump itself. Each entry point is a hand laid
;  JP whose operand is the vector, so a call costs ten T states
;  and clobbers nothing. The obvious LD HL,(vec) / JP (HL) cannot
;  be used here: HL carries the name on open and the buffer on
;  read and write, so loading the vector into it would destroy
;  the argument before the backend ever saw it.
; ============================================================
; These are esxDOS's own, which is why they were written down here
; before any backend used them. The mode byte splits into access bits
; and a disposition: $0C is the disposition, create and truncate, and
; on its own it asks for no write access at all. esxDOS 0.8.9 forgives
; that; NextZXOS is a reimplementation over +3DOS, which has always
; had a real access argument, and will not. $0E is create, truncate
; and write.
FA_READ         equ     $01
FA_OVERWRITE    equ     $0E

ST_RAM          equ     1
ST_TAPE         equ     2
ST_ESXDOS       equ     3
ST_BANK         equ     4               ; the 128K's spare banks, F3
ST_PRINT        equ     8               ; a ZX Printer, which is write only
                                        ; and has no directory, and the
                                        ; capability byte already said that
                                        ; was a thing a backend could be

STERR_NOTFOUND  equ     5
STERR_NAME      equ     7
STERR_FULL      equ     8
STERR_HANDLE    equ     9
STERR_NOSUP     equ     10              ; the backend does not do that

StOpen:         defb    $C3             ; JP nn
StVecOpen:      defw    RamOpen
StClose:        defb    $C3
StVecClose:     defw    RamClose
StRead:         defb    $C3
StVecRead:      defw    RamRead
StWrite:        defb    $C3
StVecWrite:     defw    RamWrite

; G3 needed this and STCAP_DIR had been declaring it for two
; packages with nothing behind it. A = the nth file that exists,
; counting from nought. Returns HL = its name and DE = its length,
; carry set if there is no nth file.
;
; Indexed rather than an iterator with a first and a next, because
; the caller is a panel that paints row n and wants file n, and an
; iterator would make it keep a cursor it has no use for.
StDir:          defb    $C3
StVecDir:       defw    RamDirEnt

; HL = a name. Carry set if it could not be removed, which covers
; both "no such file" and "this backend does not do that".
StDelete:       defb    $C3
StVecDelete:    defw    RamDelete

StIdent:        ld      a,(StBackend)
                ret

; A = backend id. Anything unknown leaves the safe one selected.
; ------------------------------------------------------------
;  Backend registry
;  A backend is a nine byte row: id, capabilities, and the four
;  entry points. Adding one is a row plus its four routines, and
;  nothing above this layer changes. That is the whole point of
;  building the mechanism before the backends that need it.
;
;  Capabilities exist so an application can ask rather than
;  assume. Settings on a stock 48K can be saved to tape but not
;  enumerated; on a DivMMC they can be both. The application
;  should not have to know which it is talking to.
; ------------------------------------------------------------
STCAP_WRITE     equ     $01             ; can be written at all
STCAP_PERSIST   equ     $02             ; survives a power cycle
STCAP_RANDOM    equ     $04             ; can seek, not just stream
STCAP_DIR       equ     $08             ; can enumerate names

STREGSZ         equ     14             ; id, caps, and six two byte vectors

StRegistry:
                defb    ST_RAM
                defb    STCAP_WRITE|STCAP_RANDOM|STCAP_DIR
                defw    RamOpen, RamClose, RamRead, RamWrite, RamDirEnt
                defw    RamDelete
                defb    ST_TAPE
                defb    STCAP_WRITE|STCAP_PERSIST
                defw    TapeOpen, TapeClose, TapeRead, TapeWrite, TapeDirEnt
                defw    StNoDelete
                defb    ST_BANK
                defb    STCAP_WRITE|STCAP_RANDOM|STCAP_DIR
                defw    BankOpen, BankClose, BankRead, BankWrite, BankDirEnt
                defw    BankDelete
                ; Write and persist, and nothing else. Ink on paper is as
                ; persistent as storage gets and rather harder to delete.
                ; Everything a filesystem can do. The first backend
                ; here that is not ours, and the one that survives the
                ; Next port unchanged, since NextZXOS answers the same
                ; RST 8 calls.
                defb    ST_ESXDOS
                defb    STCAP_WRITE|STCAP_PERSIST|STCAP_RANDOM|STCAP_DIR
                defw    EsxOpen, EsxClose, EsxRead, EsxWrite, EsxDirEnt
                defw    EsxDelete
                defb    ST_PRINT
                defb    STCAP_WRITE|STCAP_PERSIST
                defw    PrtOpen, PrtClose, PrtRead, PrtWrite, StNoDir
                defw    StNoDelete
                defb    $FF

; A backend that cannot enumerate says so rather than being absent
; from the table, so StDir is always safe to call and an application
; can either ask StCaps first or just try it. A tape has no
; directory, because a tape is a stream and the only way to find out
; what is on one is to play it.
StNoDir:
                ld      a,STERR_NOTFOUND
                scf
                ret

; And a backend that cannot delete. A tape cannot: the block is on
; the tape and the tape is not here.
StNoDelete:
                ld      a,STERR_NOSUP
                scf
                ret

; A = backend id. Carry set and nothing changed if it is not
; registered, so a bad id cannot leave the vectors half patched.
StSelect:
                ld      hl,StRegistry
                ld      c,a
StsLoop:
                ld      a,(hl)
                inc     a
                jr      z,StsNone
                ld      a,(hl)
                cp      c
                jr      z,StsFound
                ld      de,STREGSZ
                add     hl,de
                jr      StsLoop
StsNone:
                scf
                ret
StsFound:
                ld      (StBackend),a
                inc     hl
                ld      a,(hl)
                ld      (StCapability),a
                inc     hl
                ; The vectors are the operands of JP instructions, so they
                ; sit three bytes apart with an opcode between them. They
                ; have to be poked one at a time; an LDIR would flatten the
                ; $C3s and turn the dispatch into whatever it landed on.
                ld      de,StVecOpen
                call    StsPoke
                ld      de,StVecClose
                call    StsPoke
                ld      de,StVecRead
                call    StsPoke
                ld      de,StVecWrite
                call    StsPoke
                ld      de,StVecDir
                call    StsPoke
                ld      de,StVecDelete
                call    StsPoke
                or      a
                ret
StsPoke:
                ld      a,(hl)
                ld      (de),a
                inc     hl
                inc     de
                ld      a,(hl)
                ld      (de),a
                inc     hl
                ret

; Returns A = the capability bits of the selected backend.
StCaps:         ld      a,(StCapability)
                ret

; Called once at start up. The RAM directory lives above the heap at
; a fixed address rather than in a DEFS, so it is not in the tape
; image and its used flags are whatever was in RAM at power on.
; They have to be cleared before anything opens a file.
; Called once at start up. The 128K gets the banking backend and the
; 48K gets the RAM one, which is the whole of what F3 promised: more
; room on the bigger machine and not a separate build. Everything
; above the storage layer finds eight files of 8K instead of four of
; 256 bytes and none of it is told why.
StInit:
                call    RamInit
                call    BankInit
                call    TapeInit
                ld      a,(Is128)
                or      a
                ld      a,ST_RAM
                jr      z,StiSelect
                ld      a,ST_BANK
StiSelect:
                jp      StSelect

StBackend:      defb    ST_RAM
StCapability:   defb    0

; ============================================================
;  RAM backend
;  Four named files in a one kilobyte heap. Not a stand in for
;  the sake of having something to test: a stock 48K with no
;  interface has no storage at all, and settings and scratch data
;  need somewhere to live within a session. It is volatile and
;  makes no pretence otherwise.
;
;  A directory entry is twelve bytes:
;    name  9   up to eight characters and a terminator
;    size  2   bytes currently held
;    used  1   nonzero if the slot is taken
;
;  There is no offset field. Each file owns a fixed quarter of the
;  heap addressed from its handle, so writing never has to move
;  anything and a full file simply stops accepting bytes.
;
;  One file open at a time, which is all anything above needs, so
;  the position is a single variable rather than per handle.
; ============================================================
; A name field wide enough for a tape name, which is ten characters,
; and an entry size that is a power of two so RamEntry is four shifts
; rather than a multiply by twelve. Eight characters was never a
; decision, it was the first number that fitted, and B3 removed the
; panel width that was capping it lower still.
RAMOFFSIZE      equ     RAMNAMESZ       ; then two bytes of length
RAMOFFUSED      equ     RAMNAMESZ+2     ; then the used flag

; F1 took the window buffers out of this chain and into the heap, so
; the storage heap starts the chain and the F1 heap ends it. The two
; are unrelated things with the same word in their names: this one is
; four fixed file slots and never moves.

; A = handle 1..4. Returns HL = its directory entry.
RamEntry:
                dec     a
                ld      l,a
                ld      h,0
                add     hl,hl
                add     hl,hl
                add     hl,hl
                add     hl,hl                   ; *16, the entry size
                ld      de,RamDir
                add     hl,de
                ret

; A = handle 1..4. Returns HL = the start of its heap chunk.
RamChunk:
                dec     a
                ld      h,a
                ld      l,0                     ; *256
                ld      de,RamHeap
                add     hl,de
                ret

; Clear every used flag. RamDir is not in the tape image.
RamInit:
                ld      hl,RamDir+RAMOFFUSED
                ld      de,RAMENTSZ
                ld      b,RAMFILES
RiLoop:
                ld      (hl),0
                add     hl,de
                djnz    RiLoop
                ret

; HL = name. Copies it into RamName, zero padded to RAMNAMESZ, so
; every later comparison is a fixed nine byte compare and a caller
; may pass a short name without padding it first. Carry set if the
; name runs past ten characters with no terminator.
RamNorm:
                ld      de,RamName
                ld      b,RAMNAMESZ
RnLoop:
                ld      a,(hl)
                ld      (de),a
                inc     hl
                inc     de
                or      a
                jr      z,RnPad
                djnz    RnLoop
                ld      a,STERR_NAME
                scf
                ret
RnPad:
                dec     b                       ; bytes still to fill
                jr      z,RnDone
                xor     a
RnFill:
                ld      (de),a
                inc     de
                djnz    RnFill
RnDone:
                or      a
                ret

; Returns A = handle and carry clear if RamName matches a used
; entry, carry set otherwise.
RamFind:
                ld      c,1
RfLoop:
                ld      a,c
                cp      RAMFILES+1
                jr      nc,RfNo
                call    RamEntry
                push    hl
                ld      de,RAMOFFUSED
                add     hl,de
                ld      a,(hl)
                pop     hl
                or      a
                jr      z,RfNext
                ld      de,RamName
                ld      b,RAMNAMESZ
RfCmp:
                ld      a,(de)
                cp      (hl)
                jr      nz,RfNext
                inc     hl
                inc     de
                djnz    RfCmp
                ld      a,c
                or      a
                ret
RfNext:
                inc     c
                jr      RfLoop
RfNo:
                scf
                ret

; A = the nth used entry, counting from nought. Returns HL = its
; name, which is already a null terminated string because RamNorm
; refuses a name longer than eight characters and pads to nine, and
; DE = its length. Carry set if there is no nth file.
RamDirEnt:
                ld      c,a                     ; files still to skip
                ld      b,1                     ; the slot being looked at
RdeLoop:
                ld      a,b
                cp      RAMFILES+1
                jr      nc,RdeNone
                call    RamEntry
                push    hl
                ld      de,RAMOFFUSED
                add     hl,de
                ld      a,(hl)
                pop     hl
                or      a
                jr      z,RdeNext               ; an empty slot is not a file
                ld      a,c
                or      a
                jr      z,RdeFound
                dec     c
RdeNext:
                inc     b
                jr      RdeLoop
RdeFound:
                push    hl
                ld      de,RAMOFFSIZE
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)                  ; DE = the length
                pop     hl                      ; HL = the name
                or      a
                ret
RdeNone:
                ld      a,STERR_NOTFOUND
                scf
                ret

; HL = a name. Clears its used flag, which is the whole of delete on
; a heap where every file owns a fixed chunk: there is nothing to
; compact and nothing to move, so the slot is simply free again.
RamDelete:
                call    RamNorm
                ret     c
                call    RamFind
                ret     c
                call    RamEntry
                ld      de,RAMOFFUSED
                add     hl,de
                ld      (hl),0
                or      a
                ret

; Returns A = the first unused handle, carry set if all four are
; taken.
RamFree:
                ld      c,1
RfrLoop:
                ld      a,c
                cp      RAMFILES+1
                jr      nc,RfrNo
                call    RamEntry
                ld      de,RAMOFFUSED
                add     hl,de
                ld      a,(hl)
                or      a
                jr      z,RfrGot
                inc     c
                jr      RfrLoop
RfrGot:
                ld      a,c
                or      a
                ret
RfrNo:
                ld      a,STERR_FULL
                scf
                ret

; A = handle. Returns HL = its entry, carry set if the handle is
; out of range or names a slot nobody has opened.
RamValid:
                or      a
                jr      z,RvBad
                cp      RAMFILES+1
                jr      nc,RvBad
                ld      (RamHandle),a
                call    RamEntry
                push    hl
                ld      de,RAMOFFUSED
                add     hl,de
                ld      a,(hl)
                pop     hl
                or      a
                ret     nz                      ; OR also cleared carry
RvBad:
                ld      a,STERR_HANDLE
                scf
                ret

; HL = name, B = mode. Returns A = handle, carry on error.
RamOpen:
                ld      a,b
                ld      (RamMode),a
                call    RamNorm
                ret     c
                call    RamFind
                jr      nc,RoHave
                ld      a,(RamMode)
                cp      FA_OVERWRITE
                jr      z,RoNew
                ld      a,STERR_NOTFOUND
                scf
                ret
RoNew:
                call    RamFree
                ret     c
                ld      (RamHandle),a
                call    RamEntry
                ex      de,hl                   ; DE = entry
                ld      hl,RamName
                ld      bc,RAMNAMESZ
                ldir                            ; name in, DE -> size
                xor     a
                ld      (de),a
                inc     de
                ld      (de),a                  ; size = 0
                inc     de
                ld      a,1
                ld      (de),a                  ; used
                jr      RoReset
RoHave:
                ld      (RamHandle),a
                ld      a,(RamMode)
                cp      FA_OVERWRITE
                jr      nz,RoReset
                ld      a,(RamHandle)
                call    RamEntry
                ld      de,RAMOFFSIZE
                add     hl,de
                xor     a
                ld      (hl),a
                inc     hl
                ld      (hl),a                  ; truncate to nothing
RoReset:
                ld      hl,0
                ld      (RamPos),hl
                ld      a,(RamHandle)
                or      a
                ret

; A = handle. Nothing is buffered, so there is nothing to flush.
RamClose:
                or      a
                ret

; A = handle, HL = buffer, BC = count. Returns BC = bytes read.
RamRead:
                ld      (RamBuf),hl
                ld      (RamCnt),bc
                call    RamValid
                ret     c
                ld      de,RAMOFFSIZE
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)                  ; DE = size
                ld      hl,(RamPos)
                ex      de,hl                   ; HL = size, DE = pos
                or      a
                sbc     hl,de                   ; HL = bytes available
                ld      bc,(RamCnt)
                push    hl
                or      a
                sbc     hl,bc
                pop     hl
                jr      nc,RdN                  ; enough for the whole ask
                ld      b,h
                ld      c,l                     ; short read
RdN:
                ld      a,b
                or      c
                jr      z,RdDone
                ld      a,(RamHandle)
                call    RamChunk
                ld      de,(RamPos)
                add     hl,de                   ; HL = source
                ld      de,(RamBuf)
                push    bc
                ldir
                pop     bc
                ld      hl,(RamPos)
                add     hl,bc
                ld      (RamPos),hl
RdDone:
                or      a
                ret

; A = handle, HL = buffer, BC = count. Returns BC = bytes written.
; A file that has filled its quarter of the heap stops accepting
; bytes and reports a short write rather than failing.
RamWrite:
                ld      (RamBuf),hl
                ld      (RamCnt),bc
                call    RamValid
                ret     c
                ld      hl,RAMCHUNK
                ld      de,(RamPos)
                or      a
                sbc     hl,de                   ; HL = room left
                ld      bc,(RamCnt)
                push    hl
                or      a
                sbc     hl,bc
                pop     hl
                jr      nc,WrN
                ld      b,h
                ld      c,l                     ; short write
WrN:
                ld      a,b
                or      c
                jr      z,WrDone
                ld      a,(RamHandle)
                call    RamChunk
                ld      de,(RamPos)
                add     hl,de
                ex      de,hl                   ; DE = destination
                ld      hl,(RamBuf)
                push    bc
                ldir
                pop     bc
                ld      hl,(RamPos)
                add     hl,bc
                ld      (RamPos),hl
                ; The size is the high water mark, so overwriting the
                ; front of a file does not shorten it.
                push    bc
                ld      a,(RamHandle)
                call    RamEntry
                ld      de,RAMOFFSIZE
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)                  ; DE = size, HL -> high byte
                ld      bc,(RamPos)
                push    hl
                ex      de,hl                   ; HL = size
                or      a
                sbc     hl,bc
                pop     hl
                jr      nc,WrKeep               ; size already covers it
                ld      (hl),b
                dec     hl
                ld      (hl),c
WrKeep:
                pop     bc
WrDone:
                or      a
                ret

RamName:        defs    RAMNAMESZ
RamMode:        defb    0
RamHandle:      defb    0
RamPos:         defw    0
RamBuf:         defw    0
RamCnt:         defw    0
