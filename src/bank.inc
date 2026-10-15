; ============================================================
;  F3: the 128K banking backend
;  Where DetectMachine finally earns its place. On a 128K the
;  four banks that nothing else uses become a RAM disk, and the
;  file manager, the notepad and the settings all find more room
;  without one line of them knowing why. That is the whole point
;  of the storage registry and this is the first backend that
;  proves it: same six vectors, different machine.
;
;  The map. On a 128K, $4000 is always bank 5, $8000 is always
;  bank 2, and $C000 is whichever bank the paging port selects.
;  Bank 5 holds the screen, bank 2 holds this code, and bank 0
;  holds every buffer this system has. That leaves 1, 3, 4, 6 and
;  7 doing nothing at all.
;
;    bank 1        the directory, eight entries of sixteen bytes
;    banks 3,4,6,7 two files of 8,192 bytes each
;
;  Eight files of 8K against the 48K backend's four of 256 bytes.
;  Bank 1 gives up 16,128 bytes to hold a 128 byte directory,
;  which is wasteful and is the right trade: a directory sharing
;  a bank with file data means every directory read pages in a
;  bank that a file transfer may also want, and the saving is
;  memory this machine has no other use for.
;
;  **Paging does not need interrupts off.** The handler is at
;  $BDBD and the stack below it, both in bank 2, and IrqTick
;  touches nothing above $C000. So a transfer can page freely and
;  the frame counter keeps counting. What it cannot do is page a
;  bank in while holding a pointer into bank 0, and the caller's
;  buffer is usually exactly that. Hence the staging area below.
;
;  It was the printer buffer at $5B00, taken on the grounds that
;  nothing here had ever used it. Something does now: the printer
;  backend prints through the ROM, and the ROM buffers a line into
;  $5B00 before it drives the stylus. So the staging area moved
;  into the slow region, where it is a DEFS and the assembler
;  decides. It is contended there, which costs a bank transfer
;  perhaps a third more, and a bank transfer is a file operation
;  rather than a frame.
; ============================================================
BKDIRBANK       equ     1
BANKFILES       equ     8
BKENTSZ         equ     16              ; name 11, size 2, used 1, two spare
BKOFFSIZE       equ     RAMNAMESZ
BKOFFUSED       equ     RAMNAMESZ+2
BKCHUNK         equ     8192
BKSTAGESZ       equ     256
PAGEHOME        equ     0               ; the bank every buffer lives in

; Which bank each file's 8K sits in. The offset within it is
; (handle-1) and 1, times 8,192.
BkFileBank:     defb    3,3,4,4,6,6,7,7

; ------------------------------------------------------------
;  BankPage
;  in: A = bank 0 to 7
;  $7FFD is write only, so the other bits have to be remembered
;  rather than read back. Getting that wrong pages out the screen
;  or swaps the ROM, and both look like a crash.
; ------------------------------------------------------------
BankPage:
                ; BC and DE are put back. Writing the port needs BC for the
                ; address, and a paging routine that eats the register a
                ; caller is holding a handle in is a trap: the first
                ; version returned handle 253, which is the low byte of
                ; $7FFD and nothing else.
                push    bc
                push    de
                and     7
                ld      e,a
                ld      a,(PageCur)
                and     $F8
                or      e
                ld      (PageCur),a
                ld      bc,PAGEPORT
                out     (c),a
                pop     de
                pop     bc
                ret

BankHome:
                ld      a,PAGEHOME
                jr      BankPage

; ------------------------------------------------------------
;  BankWallBoot
;  The tape's second block is the wallpaper band, loaded into the
;  screen because that is the one place 4,096 bytes can land
;  without being under something. On a 128K it is copied into
;  bank 1, above the directory, before anything paints; on a 48K
;  it is left to InitScreen to clear and the picture is not on
;  offer. Both the source and the destination must be visible at
;  once, which is why the loader did not put it at $C000.
; ------------------------------------------------------------
BankWallBoot:
                ld      a,(Is128)
                or      a
                ret     z
                ld      a,WALLBANK
                call    BankPage
                ld      hl,WALLLOAD
                ld      de,WALLADDR
                ld      bc,WALLSIZE
                ldir
                jp      BankHome

; ------------------------------------------------------------
;  BkEntry
;  in:  A = handle 1..BANKFILES
;  out: HL = its directory entry, with bank 1 paged in
;  The caller must call BankHome before touching anything of its
;  own again.
; ------------------------------------------------------------
BkEntry:
                dec     a
                ld      l,a
                ld      h,0
                add     hl,hl
                add     hl,hl
                add     hl,hl
                add     hl,hl                   ; *16, the entry size
                ld      de,$C000
                add     hl,de
                push    hl
                ld      a,BKDIRBANK
                call    BankPage
                pop     hl
                ret

; ------------------------------------------------------------
;  BkData
;  in:  A = handle 1..BANKFILES
;  out: HL = the file's first byte, with its bank paged in
; ------------------------------------------------------------
BkData:
                dec     a
                ld      e,a
                ld      d,0
                ld      hl,BkFileBank
                add     hl,de
                ld      c,(hl)                  ; C = the bank
                ld      a,e
                and     1
                ld      h,$C0
                ld      l,0
                or      a
                jr      z,BkdLow
                ld      h,$E0                   ; the upper 8K of the bank
BkdLow:
                push    hl
                ld      a,c
                call    BankPage
                pop     hl
                ret

; ------------------------------------------------------------
;  BankInit
;  Clears every used flag. The banks hold whatever was in RAM at
;  power on, and DetectMachine has written a byte into bank 1 as
;  part of finding out this is a 128K at all, so the directory is
;  never trustworthy until this has run.
; ------------------------------------------------------------
BankInit:
                ld      a,(Is128)
                or      a
                ret     z
                ld      b,BANKFILES
                ld      c,1
BkiLoop:
                push    bc
                ld      a,c
                call    BkEntry
                ld      de,BKOFFUSED
                add     hl,de
                ld      (hl),0
                pop     bc
                inc     c
                djnz    BkiLoop
                jp      BankHome

; ------------------------------------------------------------
;  BkFind
;  in:  RamName holds a normalised name
;  out: A = handle and carry clear if it exists
;  Leaves bank 0 paged either way.
; ------------------------------------------------------------
BkFind:
                ld      c,1
BkfLoop:
                ld      a,c
                cp      BANKFILES+1
                jr      nc,BkfNone
                call    BkEntry
                push    hl
                ld      de,BKOFFUSED
                add     hl,de
                ld      a,(hl)
                pop     hl
                or      a
                jr      z,BkfNext
                ld      de,RamName
                ld      b,RAMNAMESZ
BkfCmp:
                ld      a,(de)
                cp      (hl)
                jr      nz,BkfNext
                inc     hl
                inc     de
                djnz    BkfCmp
                call    BankHome
                ld      a,c
                or      a
                ret
BkfNext:
                inc     c
                jr      BkfLoop
BkfNone:
                call    BankHome
                scf
                ret

; out: A = the first unused handle, carry set if all are taken
BkFree:
                ld      c,1
BkrLoop:
                ld      a,c
                cp      BANKFILES+1
                jr      nc,BkrNone
                call    BkEntry
                ld      de,BKOFFUSED
                add     hl,de
                ld      a,(hl)
                or      a
                jr      z,BkrGot
                inc     c
                jr      BkrLoop
BkrGot:
                call    BankHome
                ld      a,c
                or      a
                ret
BkrNone:
                call    BankHome
                scf
                ret

; ------------------------------------------------------------
;  The six vectors
; ------------------------------------------------------------
; HL = name, B = mode. Returns A = handle.
BankOpen:
                ld      a,b
                ld      (BankMode),a
                call    RamNorm                 ; one place decides what a
                ret     c                       ; name may be
                call    BkFind
                jr      nc,BkoHave
                ld      a,(BankMode)
                cp      FA_OVERWRITE
                jr      z,BkoNew
                ld      a,STERR_NOTFOUND
                scf
                ret
BkoNew:
                call    BkFree
                ret     c
                ld      (BankHandle),a
                call    BkEntry
                ex      de,hl
                ld      hl,RamName
                ld      bc,RAMNAMESZ
                ldir                            ; DE now points at the size
                xor     a
                ld      (de),a
                inc     de
                ld      (de),a
                inc     de
                ld      a,1
                ld      (de),a                  ; used
                call    BankHome
                jr      BkoReset
BkoHave:
                ld      (BankHandle),a
                ld      a,(BankMode)
                cp      FA_OVERWRITE
                jr      nz,BkoReset
                ld      a,(BankHandle)
                call    BkEntry
                ld      de,BKOFFSIZE
                add     hl,de
                xor     a
                ld      (hl),a
                inc     hl
                ld      (hl),a                  ; truncate to nothing
                call    BankHome
BkoReset:
                ld      hl,0
                ld      (BankPos),hl
                ld      a,(BankHandle)
                or      a
                ret

BankClose:
                or      a
                ret

; ------------------------------------------------------------
;  BankRead and BankWrite
;  in:  HL = the caller's buffer, BC = bytes wanted
;  out: BC = bytes actually moved
;
;  Everything goes through the staging area, 256 bytes at a time.
;  A caller's buffer is usually in bank 0 and the file is not, so
;  the two are never addressable at once and there is no clever
;  way round it.
; ------------------------------------------------------------
BankRead:
                ld      (BkUserPtr),hl
                ld      (BkWant),bc
                ld      hl,0
                ld      (BkDone),hl
                ld      a,(BankHandle)
                call    BkEntry
                ld      de,BKOFFSIZE
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                call    BankHome
                ld      hl,(BankPos)            ; bytes left in the file
                ex      de,hl
                or      a
                sbc     hl,de
                jr      c,BkrEmpty
                ld      (BkLeft),hl
                ld      de,(BkWant)             ; the smaller of the two
                or      a
                sbc     hl,de
                jr      nc,BkrClamped
                ld      hl,(BkLeft)
                ld      (BkWant),hl
BkrClamped:
                ld      a,1
                ld      (BkIsRead),a
                jp      BkMove
BkrEmpty:
                ld      bc,0
                ret

BankWrite:
                ld      (BkUserPtr),hl
                ld      (BkWant),bc
                ld      hl,0
                ld      (BkDone),hl
                ld      hl,BKCHUNK              ; room left in the file
                ld      de,(BankPos)
                or      a
                sbc     hl,de
                jr      c,BkwFull
                ld      (BkLeft),hl
                ld      de,(BkWant)
                or      a
                sbc     hl,de
                jr      nc,BkwClamped
                ld      hl,(BkLeft)
                ld      (BkWant),hl
BkwClamped:
                xor     a
                ld      (BkIsRead),a
                call    BkMove
                ; the file is as long as the furthest byte written
                push    bc
                ld      a,(BankHandle)
                call    BkEntry
                ld      de,BKOFFSIZE
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                push    hl
                ld      hl,(BankPos)
                or      a
                sbc     hl,de
                pop     hl
                jr      c,BkwNoGrow
                ld      de,(BankPos)
                ld      (hl),d
                dec     hl
                ld      (hl),e
BkwNoGrow:
                call    BankHome
                pop     bc
                ret
BkwFull:
                ld      bc,0
                ret

; Moves BkWant bytes between the file and the caller's buffer, in
; staging sized pieces, and returns BC = how many moved.
BkMove:
BkmLoop:
                ld      hl,(BkWant)
                ld      a,h
                or      l
                jp      z,BkmDone
                ld      de,BKSTAGESZ            ; this pass moves the smaller
                or      a                       ; of what is left and the
                sbc     hl,de                   ; staging area
                jr      nc,BkmFull
                ld      hl,(BkWant)
                jr      BkmHave
BkmFull:
                ld      hl,BKSTAGESZ
BkmHave:
                ld      (BkThis),hl

                ld      a,(BkIsRead)
                or      a
                jr      z,BkmWrite

                call    BkFilePtr               ; file -> staging
                ld      de,BKSTAGE
                ld      bc,(BkThis)
                ldir
                call    BankHome
                ld      hl,BKSTAGE              ; staging -> caller
                ld      de,(BkUserPtr)
                ld      bc,(BkThis)
                ldir
                ld      (BkUserPtr),de
                jr      BkmStep
BkmWrite:
                ld      hl,(BkUserPtr)          ; caller -> staging
                ld      de,BKSTAGE
                ld      bc,(BkThis)
                ldir
                ld      (BkUserPtr),hl
                call    BkFilePtr
                ex      de,hl                   ; staging -> file
                ld      hl,BKSTAGE
                ld      bc,(BkThis)
                ldir
                call    BankHome
BkmStep:
                ld      hl,(BankPos)
                ld      de,(BkThis)
                add     hl,de
                ld      (BankPos),hl
                ld      hl,(BkDone)
                ld      de,(BkThis)
                add     hl,de
                ld      (BkDone),hl
                ld      hl,(BkWant)
                ld      de,(BkThis)
                or      a
                sbc     hl,de
                ld      (BkWant),hl
                jp      BkmLoop         ; jp: relative jumps go out of range
                                        ; easily in this codebase
BkmDone:
                ld      bc,(BkDone)
                ret

; HL = where in the paged bank the file's current position is, with
; that bank selected.
BkFilePtr:
                ld      a,(BankHandle)
                call    BkData
                ld      de,(BankPos)
                add     hl,de
                ret

; ------------------------------------------------------------
;  BankDirEnt
;  in:  A = the nth file that exists
;  out: HL = its name, DE = its length
;
;  The name is copied out into BkName rather than pointed at,
;  because the entry lives in bank 1 and the caller will not have
;  it paged in. So the pointer is only good until the next call,
;  which is the ordinary contract and is what the file manager
;  now assumes of every backend rather than of one.
; ------------------------------------------------------------
BankDirEnt:
                ld      c,a
                ld      b,1
BkdeLoop:
                ld      a,b
                cp      BANKFILES+1
                jr      nc,BkdeNone
                call    BkEntry
                push    hl
                ld      de,BKOFFUSED
                add     hl,de
                ld      a,(hl)
                pop     hl
                or      a
                jr      z,BkdeNext
                ld      a,c
                or      a
                jr      z,BkdeFound
                dec     c
BkdeNext:
                call    BankHome
                inc     b
                jr      BkdeLoop
BkdeFound:
                push    hl
                ld      de,BkName
                ld      bc,RAMNAMESZ
                ldir
                pop     hl
                ld      de,BKOFFSIZE
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                push    de
                call    BankHome
                pop     de
                ld      hl,BkName
                or      a
                ret
BkdeNone:
                call    BankHome
                ld      a,STERR_NOTFOUND
                scf
                ret

; HL = a name. Frees its slot.
BankDelete:
                call    RamNorm
                ret     c
                call    BkFind
                ret     c
                call    BkEntry
                ld      de,BKOFFUSED
                add     hl,de
                ld      (hl),0
                call    BankHome
                or      a
                ret

BankMode:       defb    0
BankHandle:     defb    0
BankPos:        defw    0
BkUserPtr:      defw    0
BkWant:         defw    0
BkDone:         defw    0
BkLeft:         defw    0
BkThis:         defw    0
BkIsRead:       defb    0
BkName:         defs    RAMNAMESZ
PageCur:        defb    $10             ; ROM 1, screen 5, bank 0
