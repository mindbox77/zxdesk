; ============================================================
;  Application interface
;  A jump table at $8000. Slot n is at $8000 + 3n.
;  Slot 0 is the cold start. Order is append only.
;  API_VER goes up when a slot is added.
; ============================================================
API_VER         equ     3

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

                ; version 2: drawing relative to the live window.
                ; B = pixel rows below the title bar, C = columns in
ApiWinRec:      jp      WinRecGet       ; 28 -> HL = live record: X, Y, W, H
ApiWinPrint:    jp      WinPrint        ; 29 B, C, HL = string. Clipped
ApiWinPrintInv: jp      WinPrintInv     ; 30 the same, inverted
ApiWinClear:    jp      WinClear        ; 31 blank the interior
ApiWinAddr:     jp      WinAddr         ; 32 B, C -> HL = screen address

                ; version 3: what the notepad and the commander needed
ApiWinRows:     jp      WinRows         ; 33 -> A = rows inside the window
ApiWinCols:     jp      WinCols         ; 34 -> A = columns inside it
ApiWinApp:      jp      WinAppGet       ; 35 -> HL = the live window's descriptor
ApiWndCloseNow: jp      WndCloseNow     ; 36 close the front window, unasked
ApiKbdMods:     jp      KbdModsGet      ; 37 -> A: bit 0 CAPS, bit 1 SYMBOL
ApiWinPrintIf:  jp      WinPrintIf      ; 38 as 29, inverted if A is not 0
ApiDlgAlert:    jp      DlgAlert        ; 39 HL = line, DE = second line or 0
ApiDlgConfirm:  jp      DlgConfirm      ; 40 HL, DE, BC = callback, A = 1 yes
ApiDlgSave3:    jp      DlgSave3        ; 41 HL, DE, BC = callback, A = 0 1 2
ApiOpenFile:    jp      SysOpenFile     ; 42 HL = name. Carry if no window
ApiWinFill:     jp      WinFill         ; 43 B, C, D = rows, E = columns, HL
ApiEnd:

API_SLOTS       equ     (ApiEnd-Main)/3

ApiVerGet:
                ld      a,API_VER
                ld      b,API_SLOTS
                ret

WinRecGet:
                ld      hl,WinRec
                ret

WinPrint:
                xor     a
                ld      (PrintInv),a
WinPrintGo:
                ld      a,(WinY)
                add     a,WinCapH
                add     a,b
                ld      (PxRow),a
                ld      a,(WinX)
                inc     a
                add     a,c
                ld      c,a
                jp      WinPrintClip

WinPrintInv:
                ld      a,1
                ld      (PrintInv),a
                call    WinPrintGo
                xor     a
                ld      (PrintInv),a
                ret

WinClear:
                ld      a,(WinX)
                inc     a
                ld      (FrX),a
                ld      a,(WinY)
                add     a,WinCapH+1
                ld      (FrY),a
                ld      a,(WinW)
                sub     2
                ld      (FrW),a
                ld      a,(WinH)
                sub     WinCapH+3
                ld      (FrH),a
                ld      hl,0
                ld      (FrPat),hl
                jp      DevFillRect

WinAddr:
                ld      a,(WinX)
                inc     a
                add     a,c
                ld      c,a
                ld      a,(WinY)
                add     a,WinCapH
                add     a,b
                jp      AddrAt

; Rows between the title bar and the bottom border, and columns
; between the side borders. Both keep every register but AF.
WinRows:
                ld      a,(WinH)
                sub     WinCapH+1
                ret

WinCols:
                ld      a,(WinW)
                sub     2
                ret

WinAppGet:
                ld      hl,(WinApp)
                ret

WndCloseNow:
                ld      a,1
                ld      (WndForce),a
                jp      WndClose

KbdModsGet:
                ld      a,(KbdModState)
                ret

WinPrintIf:
                or      a
                jp      z,WinPrint
                jp      WinPrintInv

; The caller keeps the rectangle inside the window.
WinFill:
                ld      (FrPat),hl
                ld      a,(WinX)
                inc     a
                add     a,c
                ld      (FrX),a
                ld      a,(WinY)
                add     a,WinCapH
                add     a,b
                ld      (FrY),a
                ld      a,e
                ld      (FrW),a
                ld      a,d
                ld      (FrH),a
                jp      DevFillRect
