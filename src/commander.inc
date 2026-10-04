; ============================================================
;  Two pane file manager
;  A commander, in the sense the word had when Norton and then
;  GEM's desktop meant it: two listings side by side, one of them
;  live, and everything you do goes from the live one to the
;  other. That shape exists because copying is the thing people
;  actually do with files, and it needs a source and a
;  destination on screen at once.
;
;  There are no directories on this machine and there never will
;  be, so the panes are not directories. They are devices, and
;  the storage layer has had three of them since E1: RAM, tape
;  and, on a 128K, the spare banks. Two panes over two devices
;  with a copy between them is what a commander is for here, and
;  it is a truer use of the abstraction than pretending a flat
;  store has folders in it.
;
;  It is an application rather than a panel, and that is F2
;  earning its keep. A panel would have wanted a save under
;  thirty columns wide, and SUMAXW is sixteen: the arena would
;  have had to grow by nearly three kilobytes of heap. A window
;  takes its pixels from the heap already, sized to itself, and
;  gets a title bar, a close box and a resize grip for nothing.
;
;  A pane on a device with no directory says so. That is not an
;  error path bolted on, it is STCAP_DIR being asked, and the
;  tape answering honestly is the reason the capability byte
;  exists.
; ============================================================
CMDW            equ     30
CMDH            equ     96
CMDPANEW        equ     13              ; characters of listing in a pane
CMDHEADY        equ     WinCapH+2       ; the device name
CMDLISTY        equ     WinCapH+12      ; the first file under it
CMDSTSZ         equ     5

CmdState:
CmdBk0:         defb    ST_RAM          ; the device each pane is showing
CmdBk1:         defb    ST_TAPE
CmdSel0:        defb    0               ; and the row each is pointing at
CmdSel1:        defb    0
CmdActive:      defb    0               ; 0 left, 1 right

; ------------------------------------------------------------
;  CmdInit
;  The left pane opens on whatever StInit chose for this machine,
;  which is the banks on a 128K and RAM on a 48K, and the right
;  on tape. Two panes both showing the same device would be one
;  pane drawn twice.
; ------------------------------------------------------------
CmdInit:
                call    ApiStIdent
                ld      (CmdBk0),a
                cp      ST_TAPE
                ld      a,ST_TAPE
                jr      nz,CmiRight
                ld      a,ST_RAM
CmiRight:
                ld      (CmdBk1),a
                xor     a
                ld      (CmdSel0),a
                ld      (CmdSel1),a
                ld      (CmdActive),a
                ret

; ------------------------------------------------------------
;  CmdUse
;  in: A = pane. Selects that pane's device and leaves the
;      pane's index in CmdPane.
;  Every listing and every transfer goes through here, so there
;  is one place that knows a pane is a device and the rest of
;  this file only knows about panes.
; ------------------------------------------------------------
CmdUse:
                ld      (CmdPane),a
                or      a
                ld      a,(CmdBk0)
                jr      z,CmuGo
                ld      a,(CmdBk1)
CmuGo:
                jp      ApiStSelect

; out: HL = the selected row byte of CmdPane's pane
CmdSelAt:
                ld      a,(CmdPane)
                or      a
                ld      hl,CmdSel0
                ret     z
                ld      hl,CmdSel1
                ret

; out: A = how many rows of listing this window is tall
CmdRows:
                call    ApiWinRows
                sub     CMDLISTY-WinCapH+2
                jr      c,CmrOne
                srl     a
                srl     a
                srl     a
                jr      z,CmrOne
                ret
CmrOne:
                ld      a,1
                ret

; out: A = files on CmdPane's device, or nought if it has no
;      directory to ask
; in: A = pane. It took the pane from CmdPane instead, which is
; whatever the last thing to call CmdUse left there, and the last
; thing is always CmdDraw finishing with the right hand pane. So DOWN
; asked the tape how many files it had, was told none, and refused to
; move in a listing of three.
CmdCount:
                call    CmdUse
                call    ApiStCaps
                and     STCAP_DIR
                jr      z,CmcNone
                ld      b,0
CmcLoop:
                ld      a,b
                push    bc
                call    ApiStDir
                pop     bc
                jr      c,CmcDone
                inc     b
                ld      a,b
                cp      99
                jr      c,CmcLoop
CmcDone:
                ld      a,b
                ret
CmcNone:
                xor     a
                ret

; ------------------------------------------------------------
;  CmdScrollInfo
;  A unit is a file on the live pane's device. A device with no
;  directory has nothing to scroll and says so by reporting
;  nought, which WndScrollInfo reads as no bar.
; ------------------------------------------------------------
CmdScrollInfo:
                call    ApiStIdent
                ld      (CmdWasS),a
                ld      a,(CmdActive)
                call    CmdCount
                ld      (CmdTotal),a
                ld      a,(CmdWasS)
                call    ApiStSelect
                call    CmdRows
                ld      c,a
                ld      a,(CmdTotal)
                ld      b,a
                ld      a,(CmdActive)
                ld      (CmdPane),a
                call    CmdSelAt
                ld      a,(hl)                  ; the bar tracks the selection
                ret

; ------------------------------------------------------------
;  CmdScrollTo
;  in: A = the file to point at
;  The commander's listing always starts at the first file, so
;  what its bar tracks is the selection rather than a view
;  offset. That is what a list box's slider does, and it is
;  honest: there is no second thing here that could move.
; ------------------------------------------------------------
CmdScrollTo:
                ld      (CmdWant),a
                call    ApiStIdent
                ld      (CmdWasS),a
                ld      a,(CmdActive)
                call    CmdCount
                ld      b,a
                ld      a,(CmdWasS)
                call    ApiStSelect
                ld      a,b
                or      a
                ret     z
                dec     a
                ld      b,a                     ; the last file there is
                ld      a,(CmdWant)
                cp      b
                jr      c,CmdStSet
                ld      a,b
CmdStSet:
                ld      (CmdWant),a
                ld      a,(CmdActive)
                ld      (CmdPane),a
                call    CmdSelAt
                ld      a,(CmdWant)
                ld      (hl),a
                jp      CmdRepaint

; ------------------------------------------------------------
;  CmdDraw
;  F2's draw vector. Two panes, a rule between them, and the
;  device each is showing written over it.
; ------------------------------------------------------------
; Selecting a device is a global act: StBackend is one byte and every
; other part of the system reads it. So both public entry points here
; put back exactly what they found, and they use separate cells for it
; because a key ends by drawing and one cell would have the inner call
; overwriting what the outer one saved.
CmdDraw:
                call    ApiStIdent
                ld      (CmdWasD),a
                xor     a
                call    CmdPane1
                ld      a,1
                call    CmdPane1
                ld      a,(CmdWasD)
                jp      ApiStSelect

; A = pane. Draws its header and its listing.
CmdPane1:
                call    CmdUse
                ; where this pane starts
                ld      a,(CmdPane)
                or      a
                ld      a,0
                jr      z,CmpLeft
                ld      a,CMDPANEW+2
CmpLeft:
                ld      (CmdCol),a
                ; the header: the device, inverted when this is the pane
                ; the keys are pointing at, because a one bit display has
                ; one emphasis and this is what it is for
                ld      a,(CmdCol)
                ld      c,a
                call    CmdDevName
                ld      a,(CmdPane)
                ld      b,a
                ld      a,(CmdActive)
                cp      b
                ld      a,0
                jr      nz,CmpNotAct
                inc     a
CmpNotAct:
                ld      b,CMDHEADY-WinCapH
                call    ApiWinPrintIf
                ; and the files
                call    CmdRows
                ld      (CmdN),a
                xor     a
                ld      (CmdRow),a
CmpRow:
                ld      a,(CmdRow)
                push    af
                call    ApiStDir
                jr      nc,CmpPaint             ; HL is the name
                ; Nothing there. The first row of a device that cannot
                ; enumerate says so, and every other empty row is blank:
                ; a tape repeating NO DIR eight times would be shouting.
                ld      hl,TxtCmdBlank
                ld      a,(CmdRow)
                or      a
                jr      nz,CmpPaint
                push    hl
                call    ApiStCaps
                and     STCAP_DIR
                pop     hl
                jr      nz,CmpPaint
                ld      hl,TxtCmdNoDir
CmpPaint:
                ld      (CmdName),hl
                pop     af
                ld      b,a
                add     a,a
                add     a,a
                add     a,a
                add     a,CMDLISTY-WinCapH
                ld      (CmdPx),a
                ; the row the keys are on, in the pane the keys are in
                call    CmdSelAt
                ld      a,(hl)
                cp      b
                jr      nz,CmpPlain
                ld      a,(CmdPane)
                ld      c,a
                ld      a,(CmdActive)
                cp      c
                jr      nz,CmpPlain
                ld      a,1
                jr      CmpInv
CmpPlain:
                xor     a
CmpInv:
                ld      d,a
                ld      a,(CmdPx)
                ld      b,a
                ld      a,(CmdCol)
                ld      c,a
                ld      hl,(CmdName)
                ld      a,d
                call    ApiWinPrintIf
                ld      hl,CmdRow
                inc     (hl)
                ld      a,(hl)
                ld      hl,CmdN
                cp      (hl)
                jr      c,CmpRow
                ret

; out: HL = the name of CmdPane's device
CmdDevName:
                call    ApiStIdent
                cp      ST_TAPE
                ld      hl,TxtCmdTape
                ret     z
                cp      ST_BANK
                ld      hl,TxtCmdBank
                ret     z
                ld      hl,TxtCmdRam
                ret

; ------------------------------------------------------------
;  CmdKey
;  F2's key vector. Up and down walk a pane, left and right
;  change which pane, ENTER opens the file in a notepad of its
;  own, C copies it to the other pane and D deletes it.
;
;  Letters rather than a row of buttons, because thirty columns
;  is two listings and there is nowhere to put buttons that would
;  not be taken from the names.
; ------------------------------------------------------------
CmdKey:
                ld      (CmdKeyCh),a
                call    ApiStIdent
                ld      (CmdWasK),a
                ld      a,(CmdKeyCh)
                cp      KEY_UP
                jr      nz,CmkNotU
                ld      a,(CmdActive)
                ld      (CmdPane),a
                call    CmdSelAt
                ld      a,(hl)
                or      a
                jr      z,CmdRepaint
                dec     (hl)
                jr      CmdRepaint
CmkNotU:
                cp      KEY_DOWN
                jr      nz,CmkNotD
                ld      a,(CmdActive)
                call    CmdCount
                ld      b,a
                ld      a,(CmdActive)
                ld      (CmdPane),a
                call    CmdSelAt
                ld      a,(hl)
                inc     a
                cp      b
                jr      nc,CmdRepaint
                ld      (hl),a
                jr      CmdRepaint
CmkNotD:
                cp      KEY_LEFT
                jr      nz,CmkNotL
                xor     a
                ld      (CmdActive),a
                jr      CmdRepaint
CmkNotL:
                cp      KEY_RIGHT
                jr      nz,CmkNotR
                ld      a,1
                ld      (CmdActive),a
                jr      CmdRepaint
CmkNotR:
                cp      KEY_ENTER
                jr      nz,CmkNotEnt
                jp      CmdOpen
CmkNotEnt:
                cp      'C'
                jr      nz,CmkNotC
                call    CmdCopy
                jr      CmdRepaint
CmkNotC:
                cp      'D'
                ret     nz
                call    CmdDelete
                ; fall through

CmdRepaint:
                ld      a,(CmdWasK)
                call    ApiStSelect
                call    ApiPtrRestore
                call    ApiWinClear
                call    CmdDraw
                call    ApiWndBar            ; the interior was repainted
                                                ; without WinDraw, so the
                                                ; chrome it cleared goes back
                call    ApiWinGrab
                call    ApiPtrSaveBg
                jp      ApiPtrDraw

; ------------------------------------------------------------
;  The selected file's name, into CmdWork
;  Returns carry set if the pane has nothing selected, which a
;  tape pane always has.
; ------------------------------------------------------------
CmdPicked:
                ld      a,(CmdActive)
                call    CmdUse
                call    CmdSelAt
                ld      a,(hl)
                call    ApiStDir
                ret     c
                ld      de,CmdWork
                ld      b,12
CmpkCopy:
                ld      a,(hl)
                ld      (de),a
                or      a
                jr      z,CmpkDone
                inc     hl
                inc     de
                djnz    CmpkCopy
                xor     a
                ld      (de),a
CmpkDone:
                or      a
                ret

; ------------------------------------------------------------
;  CmdOpen
;  The selected file, in a notepad of its own. A commander that
;  opened files into whatever notepad happened to be behind it
;  would be a commander that overwrote your work to show you a
;  file.
; ------------------------------------------------------------
CmdOpen:
                call    CmdPicked
                jr      c,CmdRepaint
                ld      hl,CmdWork
                call    ApiOpenFile
                jr      c,CmdRepaint            ; no room, and the commander
                                                ; is still live to say so
                push    af
                ld      a,(CmdWasK)
                call    ApiStSelect
                pop     af
                or      a
                ret     nz                      ; an alert is up, leave it
                jp      ApiWndRepaint

; ------------------------------------------------------------
;  CmdCopy
;  From the live pane to the other one, which is the operation
;  the whole shape exists for. One chunk, because a file here is
;  one chunk: RAMCHUNK is 256 and so is a document.
; ------------------------------------------------------------
CmdCopy:
                call    CmdPicked
                ret     c
                ld      a,(CmdActive)
                call    CmdUse
                ld      hl,CmdWork
                ld      b,FA_READ
                call    ApiStOpen
                ret     c
                ld      (CmdHandle),a
                ld      hl,CmdBuf
                ld      bc,CMDBUFSZ
                call    ApiStRead
                ld      (CmdGot),bc
                ld      a,(CmdHandle)
                call    ApiStClose
                ; the other pane
                ld      a,(CmdActive)
                xor     1
                call    CmdUse
                ld      hl,CmdWork
                ld      b,FA_OVERWRITE
                call    ApiStOpen
                ret     c
                ld      (CmdHandle),a
                ld      hl,CmdBuf
                ld      bc,(CmdGot)
                call    ApiStWrite
                ld      a,(CmdHandle)
                jp      ApiStClose

CmdDelete:
                call    CmdPicked
                ret     c
                ld      a,(CmdActive)
                call    CmdUse
                ld      hl,CmdWork
                call    ApiStDelete
                ; the row may now be past the end of a shorter listing
                ld      a,(CmdActive)
                call    CmdCount
                ld      b,a
                ld      a,(CmdActive)
                ld      (CmdPane),a
                call    CmdSelAt
                ld      a,(hl)
                cp      b
                ret     c
                ld      a,b
                or      a
                ret     z
                dec     a
                ld      (hl),a
                ret

CMDBUFSZ        equ     256

TxtCmdRam:      defb    "RAM",0
TxtCmdTape:     defb    "TAPE",0
TxtCmdBank:     defb    "BANK",0
TxtCmdNoDir:    defb    "NO DIR",0
TxtCmdBlank:    defb    " ",0

CmdPane:        defb    0
CmdCol:         defb    0
CmdPx:          defb    0
CmdRow:         defb    0
CmdN:           defb    0
CmdName:        defw    0
CmdWasD:        defb    0
CmdWasK:        defb    0
CmdKeyCh:       defb    0
CmdWasS:        defb    0
CmdTotal:       defb    0
CmdWant:        defb    0
CmdHandle:      defb    0
CmdGot:         defw    0
CmdWork:        defs    13
CmdBuf:         defs    CMDBUFSZ
