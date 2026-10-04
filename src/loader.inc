; ============================================================
;  Loadable apps
;  File: 'Z','X','A', API version needed, image length N (word),
;  relocation count R (word). Then N bytes assembled at origin 0,
;  the 22 byte descriptor first. Then R words: offsets into the
;  image of words that get the load address added.
;  The image lives in a heap block. LDRSLOTS apps at a time.
;  A file asking for version 3 or less has a 22 byte descriptor
;  with no tick vector.
; ============================================================
HEAPOWN_APP     equ     $20
LDRHDRSZ        equ     8

; ------------------------------------------------------------
;  SysOpenFile
;  in:  HL = a name on the selected backend
;  out: carry: nothing opened. Else A = 0: a window opened,
;       A = 1: an alert is up, so do not repaint over it
;  An app file is run. Anything else opens in a notepad.
; ------------------------------------------------------------
SysOpenFile:
                call    LdrLoadName
                cp      2
                jr      z,SofDoc
                or      a                       ; clears the carry
                ret
SofDoc:
                ld      a,APP_NOTE
                call    WndOpen
                ret     c
                ld      a,(SysOpenBk)
                call    StSelect
                ld      hl,SysOpenName
                call    NoteLoadAs
                xor     a
                ret

; ------------------------------------------------------------
;  LdrLoadName
;  in:  HL = a name on the selected backend
;  out: A = 0 running, 1 an alert is up, 2 not an app or not found
;  The name is copied first: opening a window swaps state.
; ------------------------------------------------------------
LdrLoadName:
                ld      de,SysOpenName
                ld      b,NOTENAMEMAX
LdnCopy:
                ld      a,(hl)
                ld      (de),a
                or      a
                jr      z,LdnNamed
                inc     hl
                inc     de
                djnz    LdnCopy
                xor     a
                ld      (de),a
LdnNamed:
                call    StIdent
                ld      (SysOpenBk),a
IFDEF NOEXTRAS
                ld      a,2                     ; no loader in this build
                ret
ELSE
                ld      hl,SysOpenName
                ld      b,FA_READ
                call    StOpen
                jp      c,LdrNot
                ld      (LdrHandle),a
                ld      hl,LdrHdr
                ld      bc,LDRHDRSZ
                call    StRead
                ld      a,c
                cp      LDRHDRSZ
                jp      nz,LdrNotShut
                ld      hl,(LdrHdr)
                ld      de,'Z'+256*'X'
                or      a
                sbc     hl,de
                jp      nz,LdrNotShut
                ld      a,(LdrHdr+2)
                cp      'A'
                jp      nz,LdrNotShut
                ld      hl,TxtLdrOld
                ld      a,(LdrHdr+3)
                cp      API_VER+1
                jp      nc,LdrFail
                ; give back every loaded app that no window is running
                xor     a
LdrSweep:
                ld      (LdrSlot),a
                call    LdrUsedAt
                ld      a,(hl)
                or      a
                jr      z,LdrSwNext
                ld      a,(LdrSlot)
                add     a,APP_EXT
                call    AppAt
                call    WndFindApp              ; carry if no window runs it
                jr      nc,LdrSwNext
                ld      a,(LdrSlot)
                add     a,HEAPOWN_APP
                call    HeapFreeOwner
                call    LdrUsedAt
                ld      (hl),0
LdrSwNext:
                ld      a,(LdrSlot)
                inc     a
                cp      LDRSLOTS
                jr      c,LdrSweep
                ; and take the first slot that is free
                xor     a
LdrFind:
                ld      (LdrSlot),a
                call    LdrUsedAt
                ld      a,(hl)
                or      a
                jr      z,LdrFresh
                ld      a,(LdrSlot)
                inc     a
                cp      LDRSLOTS
                jr      c,LdrFind
                ld      hl,TxtLdrBusy
                jp      LdrFail
LdrFresh:
                ld      hl,(LdrHdr+4)
                ld      de,APPSIZE-2
                or      a
                sbc     hl,de
                jp      c,LdrBad                ; no room for a descriptor
                ld      hl,(LdrHdr+6)
                add     hl,hl
                jp      c,LdrBad
                ld      de,(LdrHdr+4)
                add     hl,de
                jp      c,LdrBad
                ld      (LdrSize),hl
                ld      b,h
                ld      c,l
                ld      a,(LdrSlot)
                add     a,HEAPOWN_APP
                call    HeapAlloc
                jr      nc,LdrGot
                ld      hl,TxtLdrRoom
                jp      LdrFail
LdrGot:
                ld      (LdrBase),hl
                ld      bc,(LdrSize)
                ld      a,(LdrHandle)
                call    StRead
                ld      hl,(LdrSize)
                or      a
                sbc     hl,bc
                jr      nz,LdrBad               ; the file is short
                ld      hl,(LdrBase)
                ld      de,(LdrHdr+4)
                add     hl,de                   ; HL = the table
                ld      bc,(LdrHdr+6)
LdrRel:
                ld      a,b
                or      c
                jr      z,LdrDone
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                inc     hl
                push    hl
                ld      hl,(LdrHdr+4)
                dec     hl
                or      a
                sbc     hl,de                   ; both bytes inside the image?
                jr      c,LdrBadPop
                jr      z,LdrBadPop
                ld      hl,(LdrBase)
                add     hl,de
                ld      de,(LdrBase)
                ld      a,(hl)
                add     a,e
                ld      (hl),a
                inc     hl
                ld      a,(hl)
                adc     a,d
                ld      (hl),a
                pop     hl
                dec     bc
                jr      LdrRel
LdrDone:
                call    LdrShut
                ld      a,(LdrSlot)
                add     a,APP_EXT
                call    AppAt
                push    hl
                ex      de,hl
                ld      hl,(LdrBase)
                ld      bc,APPSIZE
                ldir
                pop     hl
                ld      a,(LdrHdr+3)
                cp      4
                jr      nc,LdrTicks
                ld      de,APP_TICK             ; an older file: those two
                add     hl,de                   ; bytes are not a vector
                ld      (hl),0
                inc     hl
                ld      (hl),0
LdrTicks:
                call    LdrUsedAt
                ld      (hl),1
                ld      a,(LdrSlot)
                add     a,APP_EXT
                call    WndOpen
                ld      hl,TxtLdrRoom
                jr      c,LdrAlert              ; it stays loaded all the same
                xor     a
                ret
LdrBadPop:
                pop     hl
LdrBad:
                ld      a,(LdrSlot)
                add     a,HEAPOWN_APP
                call    HeapFreeOwner
                ld      hl,TxtLdrBad
LdrFail:
                push    hl
                call    LdrShut
                pop     hl
LdrAlert:
                ld      de,0
                call    DlgAlert
                ld      a,1
                ret
LdrNotShut:
                call    LdrShut
LdrNot:
                ld      a,2
                ret
LdrShut:
                ld      a,(LdrHandle)
                jp      StClose

; HL = the in use flag of slot (LdrSlot)
LdrUsedAt:
                ld      a,(LdrSlot)
                ld      hl,LdrUsed
                add     a,l
                ld      l,a
                ret     nc
                inc     h
                ret

LdrSlot:        defb    0
LdrUsed:        defs    LDRSLOTS
LdrHandle:      defb    0
LdrBase:        defw    0
LdrSize:        defw    0
LdrHdr:         defs    LDRHDRSZ
TxtLdrOld:      defb    "NEEDS NEW DESK",0
TxtLdrBusy:     defb    "CLOSE AN APP",0
TxtLdrRoom:     defb    "NO ROOM",0
TxtLdrBad:      defb    "BAD APP FILE",0
ENDIF

SysOpenBk:      defb    0
SysOpenName:    defs    NOTENAMEMAX+1
