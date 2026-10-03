; ============================================================
;  Application interface
;  A jump table at $8000. Slot n is at $8000 + 3n.
;  Slot 0 is the cold start. Order is append only.
;  API_VER goes up when a slot is added.
; ============================================================
API_VER         equ     1

                ; slot 0 is Main, in zxdesk.asm
ApiVersion:     jp      ApiVerGet       ; 1  out: A = API_VER, B = slots

                ; windows
ApiWndOpen:     jp      WndOpen         ; 2  A = kind. Carry if no slot free
ApiWndClose:    jp      WndClose        ; 3  closes the front window
ApiWndIsFront:  jp      WndIsFront      ; 4  Z if the live window is in front
ApiWndRepaint:  jp      WndRepaintAll   ; 5  the desktop and every window
ApiWndBar:      jp      WndScrollBar    ; 6  the live window's scroll bar
ApiWinGrab:     jp      WinGrab         ; 7  the live window into its buffer

                ; drawing
ApiPrint:       jp      PrintStrPx2     ; 8  HL = string, C = column, PxRow
ApiPrintClip:   jp      WinPrintClip    ; 9  the same, cut at the border
ApiFillRect:    jp      DevFillRect     ; 10 FrX, FrY, FrW, FrH, FrPat
ApiAddrAt:      jp      AddrAt          ; 11 A = row, C = column -> HL

                ; pointer: restore before painting, save and draw after
ApiPtrRestore:  jp      PtrRestore      ; 12
ApiPtrSaveBg:   jp      PtrSaveBg       ; 13
ApiPtrDraw:     jp      PtrDraw         ; 14

                ; heap
ApiAlloc:       jp      HeapAlloc       ; 15 BC = bytes, A = owner -> HL
ApiFree:        jp      HeapFree        ; 16 HL = a payload, null ignored
ApiFreeOwner:   jp      HeapFreeOwner   ; 17 A = owner

                ; events
ApiEvPost:      jp      EvPost          ; 18 A = type, B and C. Carry if dropped

                ; storage
ApiStSelect:    jp      StSelect        ; 19 A = backend id. Carry if unknown
ApiStOpen:      jp      StOpen          ; 20 HL = name, B = mode -> A = handle
ApiStClose:     jp      StClose         ; 21 A = handle
ApiStRead:      jp      StRead          ; 22 A, HL = buffer, BC -> BC = read
ApiStWrite:     jp      StWrite         ; 23 A, HL = buffer, BC -> BC = written
ApiStDir:       jp      StDir           ; 24 A = n -> HL = name, DE = length
ApiStDelete:    jp      StDelete        ; 25 HL = name. Carry if it could not
ApiStCaps:      jp      StCaps          ; 26 -> A = capability bits
ApiStIdent:     jp      StIdent         ; 27 -> A = backend id
ApiEnd:

API_SLOTS       equ     (ApiEnd-Main)/3

ApiVerGet:
                ld      a,API_VER
                ld      b,API_SLOTS
                ret
