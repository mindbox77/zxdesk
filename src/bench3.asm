; ============================================================
;  BENCH3: contention harness
;  Included at the end of zxdesk.asm. Emits nothing unless the
;  symbol BENCH is defined:
;    pasmo --equ BENCH=1 --name bench3.tap --tapbas src/zxdesk.asm ...
;
;  Method. The 48K interrupt period is exactly 69888 T, so the
;  interrupt is the only clock on the machine that cannot drift.
;  A measurement runs from a HALT sync to the interrupt that
;  ends it, counting whole frames crossed (K) and, in the tail,
;  iterations of a 16 T loop (C). Comparing a run against an
;  empty calibration run cancels every fixed overhead:
;
;    cost = (K-K0)*(69888-118) - 16*(C-C0)
;
;  where 118 T is the exact cost of the returning interrupt
;  path, counted below. The counting loop and all harness state
;  live above $8000, so the harness itself is never contended
;  and only the code under test pays the ULA. Numbers are
;  printed raw; the arithmetic is done off the machine.
; ============================================================

IFDEF HARNESS

; ------------------------------------------------------------
;  The timing half
;  Everything from here to the end of BReport needs an
;  interrupt to anchor to and a screen to print on, so it only
;  exists in the BENCH build. The correctness subjects below it
;  need neither and are in both, because zxtest.py runs them
;  headlessly and the two harnesses were competing for the same
;  fifteen kilobytes.
; ------------------------------------------------------------
IFDEF BENCH
BNTESTS         equ     15

; ------------------------------------------------------------
;  Entry
; ------------------------------------------------------------
BenchMain:
                di
                ld      sp,STACKTOP
                call    DetectMachine
                call    SetupIM2
                ld      a,$C3           ; replace the EI/RET handler
                ld      (IRQHANDLER),a
                ld      hl,BenchIRQ
                ld      (IRQHANDLER+1),hl
                call    BuildScrTab
                call    StInit
                ; F1. The window's pixels come from the heap, so a harness
                ; that skips WndInit paints into address nought: WinBufP is
                ; no longer a fixed address the assembler filled in. F2 put
                ; the document there too, so this is also what NoteNew used
                ; to be called for.
                call    WndInit
                call    BClear
                call    WinDraw         ; regression fingerprint: any change to
                call    WinGrab         ; what WinDraw paints moves this number
                call    BScrSum
                ld      (BSum),hl
                call    BClear
                call    BlitRect        ; and the copy must land on the same number
                call    BScrSum
                ld      (BSum2),hl
                call    BClear
                ; the same six move drag, erased both ways
                ld      hl,WinErase
                ld      (BEraseVec),hl
                call    BMoveSeq
                call    BScrSum
                ld      (BSum3),hl
                ld      hl,WinEraseDamage
                ld      (BEraseVec),hl
                call    BMoveSeq
                call    BScrSum
                ld      (BSum4),hl
                call    BEvTest
                call    BHitTest
                call    BSaveUnder
                call    BMenuTest
                call    BTapeTest
                ld      a,48
                ld      (WinY),a
                ld      (WinOldY),a
                ld      a,8
                ld      (WinX),a
                ld      (WinOldX),a
                ; And the size, explicitly. The timed subjects had been
                ; running against whatever the last subject left behind,
                ; which happened to be right until the notepad's window
                ; grew two columns for its scroll bar. A timing series is
                ; only comparable across builds if the thing being timed is
                ; the same size in each of them.
                ld      a,16
                ld      (WinW),a
                ld      a,72
                ld      (WinH),a
                call    BClear
                ld      hl,BTests
                ld      (BTestPtr),hl
                ld      hl,BResults
                ld      (BResPtr),hl
                ld      b,BNTESTS
BRunLoop:
                push    bc
                ld      hl,(BTestPtr)
                inc     hl
                inc     hl              ; skip the name
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                inc     hl
                push    hl
                ex      de,hl
                call    BCallHL         ; per test setup, outside the window
                pop     hl
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                inc     hl
                ld      (BVec),de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                inc     hl
                ld      (BDelayN),de
                ld      (BTestPtr),hl
                call    BMeasure
                ld      hl,(BResPtr)
                ld      a,(BFrames)
                ld      (hl),a
                inc     hl
                ld      de,(BSaveDE)
                ld      (hl),e
                inc     hl
                ld      (hl),d
                inc     hl
                ld      (BResPtr),hl
                pop     bc
                djnz    BRunLoop
                call    BReport
                di
BHang:
                jr      BHang

BCallHL:
                jp      (hl)

; ------------------------------------------------------------
;  BMeasure
;  in:  (BVec) routine to time, (BDelayN) pre delay iterations
;  out: (BFrames) = K frames crossed, (BSaveDE) = C tail count
;
;  IX is loaded after the routine returns, so a test that
;  clobbers IX cannot derail the bail out. DE is zeroed before
;  BBail is armed, so an interrupt landing in the gap still
;  reports a valid count rather than rubbish.
; ------------------------------------------------------------
BMeasure:
                di
                xor     a
                ld      (BBail),a
                ld      (BFrames),a
                ei
                halt                    ; sync to an interrupt boundary
                xor     a
                ld      (BFrames),a     ; discard the sync interrupt
                ld      hl,(BDelayN)
                call    BDelay          ; move the start point up the frame
                call    BCallVec
                ld      ix,BMDone
                ld      de,0
                ld      a,1
                ld      (BBail),a
BMCount:
                inc     de              ; 6
                jp      BMCount         ; 10, so 16 T a turn
BMDone:
                ret

BCallVec:
                ld      hl,(BVec)
                jp      (hl)

; ------------------------------------------------------------
;  BenchIRQ, reached by a JP at IRQHANDLER
;  Returning path: 19 ack + 10 jp + 11 + 13 + 4 + 7 + 13 + 4
;                  + 13 + 10 + 4 + 10 = 118 T exactly.
;  All of it above $8000 with the stack at $BD00, so none of it
;  is contended and the count is arithmetic, not an estimate.
;  The bail path appears once in every run including the
;  calibration, so its cost cancels and is not needed.
; ------------------------------------------------------------
BenchIRQ:
                push    af
                ld      a,(BBail)
                or      a
                jr      nz,BIrqBail
                ld      a,(BFrames)
                inc     a
                ld      (BFrames),a
                pop     af
                ei
                ret
BIrqBail:
                ld      (BSaveDE),de
                pop     af              ; drop the saved AF
                pop     af              ; drop the interrupted PC
                jp      (ix)

; ------------------------------------------------------------
;  BDelay
;  in: HL iterations. Cost is 26*HL + 18 T, or 19 T for zero.
;  Touches no memory beyond its own fetches, so it is exact.
; ------------------------------------------------------------
BDelay:
                ld      a,h
                or      l
                ret     z
BDlLoop:
                dec     hl
                ld      a,h
                or      l
                jr      nz,BDlLoop
                ret

ENDIF
; ------------------------------------------------------------
;  Subjects
; ------------------------------------------------------------
BNull:
                ret

BPtrSeq:                                ; one frame of pointer work
                call    PtrRestore
                call    PtrSaveBg
                call    PtrDraw
                ret

BDmgSet:                                ; old position two rows above
                ld      a,48
                ld      (WinY),a
                ld      (WinOldY),a
                ld      a,8
                ld      (WinX),a
                ld      (WinOldX),a
                ret

BDmgDown:
                ld      a,50
                ld      (WinY),a
                call    WinEraseDamage
                ld      a,48
                ld      (WinY),a
                ret

BDmgUp:
                ld      a,46
                ld      (WinY),a
                call    WinEraseDamage
                ld      a,48
                ld      (WinY),a
                ret

BDmgRight:
                ld      a,9
                ld      (WinX),a
                call    WinEraseDamage
                ld      a,8
                ld      (WinX),a
                ret

BDmgDiag:
                ld      a,50
                ld      (WinY),a
                ld      a,9
                ld      (WinX),a
                call    WinEraseDamage
                ld      a,48
                ld      (WinY),a
                ld      a,8
                ld      (WinX),a
                ret

BDragDmg:                               ; a whole drag frame, damage erase
                call    PtrRestore
                call    PtrSaveBg
                call    PtrDraw
                call    ReadInput
                call    BDmgDown
                call    BlitRect
                call    PtrSaveBg
                call    PtrDraw
                ret

BArrDrag:                               ; arrival at the beam, then the redraw
                call    PtrRestore
                call    PtrSaveBg
                call    PtrDraw
                call    WaitBeamTopOfWin
                call    BDmgDown
                call    BlitRect
                call    PtrSaveBg
                call    PtrDraw
                ret

; Correctness harness for the damage erase. The same short drag is
; run twice, once erasing the whole old rectangle and once erasing
; only the vacated strip, and the two screens must be identical.
; Six moves: down, down, right, up, left, and a diagonal.
BEraseVia:
                ld      hl,(BEraseVec)
                jp      (hl)

BMove:                                  ; in: D = new WinY, E = new WinX
                ld      a,(WinY)
                ld      (WinOldY),a
                ld      a,(WinX)
                ld      (WinOldX),a
                ld      a,d
                ld      (WinY),a
                ld      a,e
                ld      (WinX),a
                call    BEraseVia
                call    BlitRect
                ret

BMoveSeq:
                call    BClear
                ld      a,48
                ld      (WinY),a
                ld      (WinOldY),a
                ld      a,8
                ld      (WinX),a
                ld      (WinOldX),a
                call    BlitRect
                ld      de,50*256+8
                call    BMove
                ld      de,53*256+8
                call    BMove
                ld      de,53*256+10
                call    BMove
                ld      de,51*256+10
                call    BMove
                ld      de,51*256+9
                call    BMove
                ld      de,52*256+11
                call    BMove
                ret

; ShowStatus, both ways round. It used to cost 23,008 T for
; twenty characters and was gated off during a drag rather than
; fixed. It now builds the row into a buffer and paints only the
; cells that differ, so the settled case is the compare alone and
; the moving case is a digit or two. STAT is the settled row,
; which is what most frames are; STATM is a pointer that moved.
BStatSet:
                call    ShowStatus      ; sync the shadow, so the subject
                call    ShowStatus      ; measures a row that has settled.
                ret                     ; Twice, to find out whether one
                                        ; call actually settles it

BStatBuild:                             ; the row built, nothing painted
                jp      SsBuild

BStatMove:
                ld      a,(PtrX)
                inc     a
                ld      (PtrX),a
                jp      ShowStatus

BDragFrame2:                            ; the same frame with the status bar off
                call    PtrRestore
                call    PtrSaveBg
                call    PtrDraw
                call    ReadInput
                call    WinErase
                call    BlitRect
                ret


IFDEF BENCH
; The new renderer, same inputs, on the same three cases. It must
; also land on identical pixels, which BScrSum2 checks.

BTests:
                defw    BTxtCal,   BNull,     BNull,        0
                defw    BTxtDmgD,  BDmgSet,   BDmgDown,     0
                defw    BTxtDmgU,  BDmgSet,   BDmgUp,       0
                defw    BTxtDmgR,  BDmgSet,   BDmgRight,    0
                defw    BTxtDmgX,  BDmgSet,   BDmgDiag,     0
                defw    BTxtWEr,   BDmgSet,   WinErase,     0
                defw    BTxtDrgD,  BDmgSet,   BDragDmg,     0
                defw    BTxtDrgD,  BDmgSet,   BDragDmg,     1400
                defw    BTxtDrg2,  BDmgSet,   BDragFrame2,  0
                defw    BTxtBlit,  BDmgSet,   BlitRect,     0
                defw    BTxtPtr,   BNull,     BPtrSeq,      0
                defw    BTxtAD,    BDmgSet,   BArrDrag,     0
                defw    BTxtStat,  BStatSet,  ShowStatus,   0
                defw    BTxtStatM, BStatSet,  BStatMove,    0
                defw    BTxtStatB, BStatSet,  BStatBuild,   0

BTxtHead:       defb    "SUBJECT  DLY   K COUNT",0
BTxtCal:        defb    "CAL",0
BTxtStat:       defb    "STAT",0
BTxtStatM:      defb    "STATM",0
BTxtStatB:      defb    "STATB",0
BTxtPtr:        defb    "PTR",0
BTxtWEr:        defb    "WINERASE",0
BTxtSum:        defb    "WDRW/BLT",0
BTxtBlit:       defb    "BLIT",0
BTxtDrg2:       defb    "DRAGFRME",0
BTxtDmgD:       defb    "DMG DOWN",0
BTxtDmgU:       defb    "DMG UP",0
BTxtDmgR:       defb    "DMG RGHT",0
BTxtDmgX:       defb    "DMG DIAG",0
BTxtDrgD:       defb    "DRAG DMG",0
BTxtAD:         defb    "ARR+DRAG",0
BTxtSum3:       defb    "ERAS FUL",0
BTxtSum4:       defb    "ERAS DMG",0
BTxtEv:         defb    "EVENTS",0
BTxtHit:        defb    "HITS/13",0
BTxtSuA:        defb    "SAVEUNDR",0
BTxtMn:         defb    "MENU S/R",0
BTxtMnP:        defb    "MN HIT/I",0

; ------------------------------------------------------------
;  Report
; ------------------------------------------------------------
BReport:
                call    BClear
                ld      hl,BTxtHead
                ld      b,0
                ld      c,0
                call    PrintStr
                ld      hl,BTests
                ld      (BTestPtr),hl
                ld      hl,BResults
                ld      (BResPtr),hl
                ld      a,2
                ld      (BRow),a
                ld      b,BNTESTS
BRepLoop:
                push    bc
                ld      hl,(BTestPtr)
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                inc     hl
                inc     hl
                inc     hl
                inc     hl
                inc     hl              ; past setup and subject
                push    hl
                ex      de,hl
                ld      a,(BRow)
                ld      b,a
                ld      c,0
                call    PrintStr
                pop     hl
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                inc     hl
                ld      (BTestPtr),hl
                ex      de,hl
                ld      a,(BRow)
                ld      b,a
                ld      c,9
                call    BPrintDec5
                ld      hl,(BResPtr)
                ld      a,(hl)
                inc     hl
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                inc     hl
                ld      (BResPtr),hl
                push    de
                add     a,'0'
                ld      hl,BRow
                ld      b,(hl)
                ld      c,15
                call    PrintChar
                pop     hl
                ld      a,(BRow)
                ld      b,a
                ld      c,17
                call    BPrintDec5
                ld      hl,BRow
                inc     (hl)
                pop     bc
                djnz    BRepLoop
                ld      hl,BTxtSum
                ld      b,17
                ld      c,0
                call    PrintStr
                ld      hl,(BSum)
                ld      b,17
                ld      c,9
                call    BPrintDec5
                ld      hl,(BSum2)
                ld      b,17
                ld      c,16
                call    BPrintDec5
                ld      hl,BTxtSum3
                ld      b,18
                ld      c,0
                call    PrintStr
                ld      hl,(BSum3)
                ld      b,18
                ld      c,9
                call    BPrintDec5
                ld      hl,BTxtSum4
                ld      b,19
                ld      c,0
                call    PrintStr
                ld      hl,(BSum4)
                ld      b,19
                ld      c,9
                call    BPrintDec5
                ld      hl,BTxtEv
                ld      b,20
                ld      c,0
                call    PrintStr
                ld      a,(BEvRes)
                ld      l,a
                ld      h,0
                ld      b,20
                ld      c,9
                call    BPrintDec5
                ld      hl,BTxtHit
                ld      b,21
                ld      c,0
                call    PrintStr
                ld      a,(BHitRes)
                ld      l,a
                ld      h,0
                ld      b,21
                ld      c,9
                call    BPrintDec5
                ld      hl,BTxtSuA
                ld      b,20
                ld      c,0
                call    PrintStr
                ld      hl,(BSuA)
                ld      b,20
                ld      c,9
                call    BPrintDec5
                ld      hl,(BSuB)
                ld      b,20
                ld      c,16
                call    BPrintDec5
                ld      hl,BTxtMn
                ld      b,21
                ld      c,0
                call    PrintStr
                ld      hl,(BMnA)
                ld      b,21
                ld      c,9
                call    BPrintDec5
                ld      hl,(BMnB)
                ld      b,21
                ld      c,16
                call    BPrintDec5
                ld      hl,BTxtMnP
                ld      b,22
                ld      c,0
                call    PrintStr
                ld      a,(BMnHit)
                ld      l,a
                ld      h,0
                ld      b,22
                ld      c,9
                call    BPrintDec5
                ld      a,(BMnItem)
                ld      l,a
                ld      h,0
                ld      b,22
                ld      c,16
                call    BPrintDec5
                ld      hl,BTxtTp
                ld      b,23
                ld      c,0
                call    PrintStr
                ld      hl,(BTpRead)
                ld      b,23
                ld      c,6
                call    BPrintDec5
                ld      a,(BTpSum)
                ld      l,a
                ld      h,0
                ld      b,23
                ld      c,12
                call    BPrintDec5
                ld      a,(BTpCaps)
                ld      l,a
                ld      h,0
                ld      b,23
                ld      c,18
                call    BPrintDec5
                ld      a,(BTpErr)
                ld      l,a
                ld      h,0
                ld      b,23
                ld      c,24
                call    BPrintDec5
                ret

ENDIF
; Rotating checksum of the pixel file, so a byte moving between two
; positions changes the result. A plain sum would not notice.
BScrSum:
                ld      hl,0
                ld      de,SCREEN
                ld      bc,6144
BSsLoop:
                add     hl,hl
                jr      nc,BSsNoRot
                inc     hl
BSsNoRot:
                ld      a,(de)
                add     a,l
                ld      l,a
                jr      nc,BSsNoCy
                inc     h
BSsNoCy:
                inc     de
                dec     bc
                ld      a,b
                or      c
                jr      nz,BSsLoop
                ret

IFDEF BENCH             ; screen output, for the report only
; in: HL value, B char row, C char column
BPrintDec5:
                ld      (BNum),hl
                ld      hl,BPow10
                ld      (BPowPtr),hl
                ld      a,5
                ld      (BDigits),a
BPd5Loop:
                ld      hl,(BPowPtr)
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                inc     hl
                ld      (BPowPtr),hl
                ld      hl,(BNum)
                ld      a,'0'-1
BPd5Sub:
                inc     a
                or      a
                sbc     hl,de
                jr      nc,BPd5Sub
                add     hl,de
                ld      (BNum),hl
                push    bc
                call    PrintChar
                pop     bc
                inc     c
                ld      a,(BDigits)
                dec     a
                ld      (BDigits),a
                jr      nz,BPd5Loop
                ret
ENDIF

BClear:
                ld      hl,SCREEN
                ld      de,SCREEN+1
                ld      bc,6144-1
                ld      (hl),0
                ldir
                ld      hl,ATTRS
                ld      de,ATTRS+1
                ld      bc,768-1
                ld      (hl),ATTR_MONO
                ldir
                ret

IFDEF BENCH             ; the timing half's own state
BPow10:         defw    10000,1000,100,10,1

BVec:           defw    0
BDelayN:        defw    0
BFrames:        defb    0
BBail:          defb    0
BSaveDE:        defw    0
BTestPtr:       defw    0
BResPtr:        defw    0
BNum:           defw    0
BPowPtr:        defw    0
BDigits:        defb    0
BRow:           defb    0
BSum:           defw    0
BSum2:          defw    0
BSum3:          defw    0
BSum4:          defw    0
BResults:       defs    BNTESTS*3
ENDIF

; Which erase a move sequence uses. Both halves drive it: the bench
; times the two against each other and zxtest.py checks they land on
; the same pixels.
BEraseVec:      defw    0


IFDEF BENCH
; Bench only. zxtest.py drives none of these three: the event queue,
; the save under and the menu are all checked headlessly against the
; desktop binary instead, which is the build people run. They stayed
; in both harnesses out of habit and the TEST build no longer has the
; room for habits.
; Event queue unit test. Drives a whole drag through the queue with
; no input hardware involved, and checks the wrap-around too. Result
; is a bitmask, so 15 means all four passed and anything else says
; which one did not.
BEvTest:
                xor     a
                ld      (BEvRes),a
                ld      (Dragging),a
                ld      (EvHead),a
                ld      (EvTail),a
                ld      a,48
                ld      (WinY),a
                ld      (WinOldY),a
                ld      a,8
                ld      (WinX),a
                ld      (WinOldX),a
                ld      a,100           ; byte column 12, inside the window
                ld      (PtrX),a
                ld      a,52            ; inside the 48 to 57 title bar
                ld      (PtrY),a
                ld      bc,0
                ld      a,EV_BTNDOWN
                call    EvPost
                call    EvDispatch
                ld      a,(Dragging)    ; 1: the press started a drag
                or      a
                jr      z,BEt1
                ld      a,(BEvRes)
                or      1
                ld      (BEvRes),a
BEt1:
                ld      a,62
                ld      (PtrY),a
                ld      b,100
                ld      c,62
                ld      a,EV_PTRMOVE
                call    EvPost
                call    EvDispatch
                ld      a,(WinY)        ; 2: the move moved the window
                cp      48
                jr      z,BEt2
                ld      a,(BEvRes)
                or      2
                ld      (BEvRes),a
BEt2:
                ld      bc,0
                ld      a,EV_BTNUP
                call    EvPost
                call    EvDispatch
                ld      a,(Dragging)    ; 3: the release ended it
                or      a
                jr      nz,BEt3
                ld      a,(BEvRes)
                or      4
                ld      (BEvRes),a
BEt3:
                ld      b,20            ; 4: overfilling drops, does not wrap
BEtFill:
                push    bc
                ld      bc,0
                ld      a,EV_KEY
                call    EvPost
                pop     bc
                djnz    BEtFill
                call    EvDispatch
                ld      a,(EvHead)
                ld      b,a
                ld      a,(EvTail)
                cp      b
                jr      nz,BEt4
                ld      a,(BEvRes)
                or      8
                ld      (BEvRes),a
BEt4:
                ; 5: EvPoll turns a button state change into one event
                xor     a
                ld      (EvHead),a
                ld      (EvTail),a
                ld      a,$FF
                ld      (EvLastBtn),a
                ld      a,$FD           ; left button down, active low
                ld      (Buttons),a
                call    EvPoll
                call    EvNext
                cp      EV_BTNDOWN
                jr      nz,BEt5
                ld      a,(BEvRes)
                or      16
                ld      (BEvRes),a
BEt5:
                ; 6: and a pointer move into another
                xor     a
                ld      (EvHead),a
                ld      (EvTail),a
                ld      a,$FD
                ld      (EvLastBtn),a
                ld      a,77
                ld      (PtrX),a
                ld      a,33
                ld      (PtrY),a
                call    EvPoll
                call    EvNext
                cp      EV_PTRMOVE
                jr      nz,BEt6
                ld      a,b
                cp      77
                jr      nz,BEt6
                ld      a,c
                cp      33
                jr      nz,BEt6
                ld      a,(BEvRes)
                or      32
                ld      (BEvRes),a
BEt6:
                xor     a
                ld      (EvHead),a
                ld      (EvTail),a
                ld      (Dragging),a
                ld      (WinMoved),a
                ld      a,$FF
                ld      (EvLastBtn),a
                ld      (Buttons),a
                ld      a,120
                ld      (PtrX),a
                ld      (EvLastX),a
                ld      a,90
                ld      (PtrY),a
                ld      (EvLastY),a
                ld      a,48
                ld      (WinY),a
                ld      (WinOldY),a
                ld      a,8
                ld      (WinX),a
                ld      (WinOldX),a
                ret

BEvRes:         defb    0

ENDIF

; Hit test unit test. Each row is a byte column, a pixel row and the
; control expected there. Boundaries are included deliberately: the
; edges are where an off by one lives, and the close box sits inside
; the title bar so it also checks that the z order is respected.
BHitTest:
                xor     a
                ld      (BHitRes),a
                ld      a,48
                ld      (WinY),a
                ld      a,8
                ld      (WinX),a
                ; The size the cases were written against. It used to be
                ; whatever WinRec was assembled with, and WinRec grew two
                ; columns when the notepad got a scroll bar, which moved
                ; the window's right edge out from under three of them.
                ld      a,16
                ld      (WinW),a
                ld      a,72
                ld      (WinH),a
                ld      hl,BHitCases
                ld      b,BHITN
BHtLoop:
                push    bc
                ld      b,(hl)
                inc     hl
                ld      c,(hl)
                inc     hl
                ld      a,(hl)
                inc     hl
                push    hl
                push    af
                call    BHitOne
                ld      c,a
                pop     af
                cp      c
                pop     hl
                pop     bc
                jr      nz,BHtNext      ; a wrong answer leaves the bit clear
                push    hl
                ld      a,(BHitRes)
                inc     a
                ld      (BHitRes),a
                pop     hl
BHtNext:
                djnz    BHtLoop
                ret

BHitCases:
                defb    8,49,   CTL_CLOSEBOX    ; close box, over the title bar
                defb    8,48,   CTL_TITLEBAR    ; one row above it
                defb    9,49,   CTL_TITLEBAR    ; one column right of it
                defb    23,48,  CTL_TITLEBAR    ; last column of the bar
                defb    24,48,  CTL_DESKTOP     ; one past it
                defb    8,57,   CTL_TITLEBAR    ; last row of the bar
                defb    8,58,   CTL_WININTERIOR ; first row below it
                defb    8,119,  CTL_WININTERIOR ; last row of the window
                defb    8,120,  CTL_DESKTOP     ; one below it
                defb    0,0,    CTL_MENUBAR
                defb    31,7,   CTL_MENUBAR
                defb    0,8,    CTL_MENUBAR     ; row 8 is the rule, MENUH is 9
                defb    0,9,    CTL_DESKTOP     ; the desktop starts here
BHITN           equ     13

; ------------------------------------------------------------
;  BHitOne
;  in:  B = byte column, C = pixel row
;  out: A = what a press there would actually find
;  The subject called HitTest alone, and B4 took the close box,
;  the title bar and the window interior out of the control table
;  and gave them to WndHitTest, which walks the z order. Nine of
;  the thirteen cases then asked a routine that can no longer
;  answer them, and the row read 6 of 13 from B4 until F1 without
;  anyone reading it as a failure. A subject that cannot pass is
;  worse than no subject: it trains you to ignore the number.
;  This asks the two in the order HdlBtnDown asks them.
; ------------------------------------------------------------
BHitOne:
                push    bc
                call    HitTest
                pop     bc
                ld      (BHitChrome),a
                cp      CTL_MENUBAR             ; the bar is the one thing
                ret     z                       ; HdlBtnDown acts on directly
                push    bc
                call    WndHitTest
                pop     bc
                cp      CTL_NONE
                ret     nz
                ld      a,(BHitChrome)          ; nothing in a window, so the
                ret                             ; chrome answer stands

BHitChrome:     defb    0
BHitRes:        defb    0

IFDEF BENCH
; Bench only. zxtest.py drives none of these three: the event queue,
; the save under and the menu are all checked headlessly against the
; desktop binary instead, which is the build people run. They stayed
; in both harnesses out of habit and the TEST build no longer has the
; room for habits.
; Save under test. Fingerprint the screen, save a rectangle, scribble
; all over it, restore, and fingerprint again. The two must match.
; The scribble is deliberately a solid fill rather than something
; subtle, so a restore that only half works cannot pass by luck.
BSaveUnder:
                call    BClear
                call    InitScreen
                call    WinDraw
                call    BScrSum
                ld      (BSuA),hl
                ld      a,10            ; a rectangle straddling window and desk
                ld      (SuX),a
                ld      a,40
                ld      (SuY),a
                ld      a,10
                ld      (SuW),a
                ld      a,60
                ld      (SuH),a
                call    SaveUnder
                ld      a,10
                ld      (FrX),a
                ld      a,40
                ld      (FrY),a
                ld      a,10
                ld      (FrW),a
                ld      a,60
                ld      (FrH),a
                ld      hl,$FFFF
                ld      (FrPat),hl
                call    DevFillRect     ; obliterate it
                call    RestoreUnder
                call    BScrSum
                ld      (BSuB),hl
                call    BClear
                ret

BSuA:           defw    0
BSuB:           defw    0

; Open a menu over a live desktop, check what the hit test says about
; a point inside it, pick an item, close, and compare fingerprints.
; Save under has to put back exactly what the menu covered.
BMenuTest:
                call    BClear
                call    InitScreen
                call    WinDraw
                call    PtrSaveBg
                call    PtrDraw
                call    BScrSum
                ld      (BMnA),hl
                ld      a,2                     ; FILE
                call    MenuOpenDrop
                ld      b,11                    ; inside the drop
                ld      c,26                    ; third item, SAVE
                call    HitTest
                ld      (BMnHit),a
                ld      c,26
                call    MenuItemAt
                ld      (BMnItem),a
                call    MenuClose
                call    BScrSum
                ld      (BMnB),hl
                call    BClear
                ret

BMnA:           defw    0
BMnB:           defw    0
BMnHit:         defb    0
BMnItem:        defb    0

ENDIF

; ------------------------------------------------------------
;  E1 storage round trip
;  Writes a known pattern to a named file, closes it, reopens it
;  for reading and compares what comes back. The pattern steps by
;  seven so a stuck byte, a repeated byte or an off by one in the
;  position arithmetic all disturb it; a run of zeros would hide
;  every one of those against an untouched heap.
;
;  Acceptance: both counts are 64, the mismatch count is 0, and
;  opening a name that was never created reports error 5 rather
;  than handing back a handle to an empty slot.
; ------------------------------------------------------------
BSTN            equ     64
; These were fixed addresses, $B200 and $B240, chosen when the program
; ended a long way below them. It ends at $B8FC now, so they were
; inside it: BStoreTest was writing sixty four bytes of test pattern
; over AccelTabs at $B24F and over whatever else had grown into that
; page. It surfaced as EXTEND MODE and 5 sending the pointer to the
; right hand limit, because a pointer step of 243 underflows going
; left and lands on the clamp at the other end.
;
; A harness that scribbles on the program it is testing is worse than
; no harness. They are buffers now, and the assembler decides where.
IFDEF TEST                              ; and only the build that uses them
BStSrc:         defs    BSTN
BStDst:         defs    BSTN
ENDIF

; ------------------------------------------------------------
;  Subjects that need no interrupt and no screen
;  Pure RAM, and zxtest.py drives them with an exit status. The
;  Fuse report showed the same counts as numbers to read off a
;  screenshot, which is the weaker of the two and was the copy
;  costing bytes in the build that had 581 left. The tape subject
;  is not among them: it calls the ROM loader, and the headless
;  emulator has no tape.
; ------------------------------------------------------------
IFDEF TEST
BStoreTest:
                call    StInit
                ld      hl,BStSrc
                ld      b,BSTN
                ld      a,1
BstFill:
                ld      (hl),a
                add     a,7
                inc     hl
                djnz    BstFill
                ld      hl,BStName
                ld      b,FA_OVERWRITE
                call    StOpen
                jr      c,BstFail
                ld      (BStH),a
                ld      hl,BStSrc
                ld      bc,BSTN
                call    StWrite
                ld      (BStWrote),bc
                ld      a,(BStH)
                call    StClose
                ld      hl,BStName
                ld      b,FA_READ
                call    StOpen
                jr      c,BstFail
                ld      (BStH),a
                ld      hl,BStDst
                ld      bc,BSTN
                call    StRead
                ld      (BStRead),bc
                ld      a,(BStH)
                call    StClose
                ld      hl,BStSrc
                ld      de,BStDst
                ld      b,BSTN
                ld      c,0
BstCmp:
                ld      a,(de)
                cp      (hl)
                jr      z,BstSame
                inc     c
BstSame:
                inc     hl
                inc     de
                djnz    BstCmp
                ld      a,c
                ld      (BStBad),a
                ld      hl,BStMiss
                ld      b,FA_READ
                call    StOpen
                jr      nc,BstNoErr
                ld      (BStErr),a
                ret
BstNoErr:
                xor     a                       ; zero: it wrongly opened
                ld      (BStErr),a
                ret
BstFail:
                ld      a,$FF
                ld      (BStErr),a
                ret

BStName:        defb    "SETTINGS",0
ENDIF
BStMiss:        defb    "NOPE",0
BStH:           defb    0
BStWrote:       defw    0
BStRead:        defw    0
BStBad:         defb    0
BStErr:         defb    0
BTxtTp:         defb    "TAPE",0


; Tape backend test. taplant.py appends a block called TAPETEST to
; the very tape this build loads from, so LD_BYTES reads what follows
; the program. That exercises the real ROM loader, real bit timing
; and the real header search, not a simulation of them.
;
; The planted bytes are $DE $AD $BE $EF $01 $02 $03 $04, chosen so a
; buffer that was never written reads as zeros and fails visibly.
BTapeTest:
                ld      a,ST_TAPE
                call    StSelect
                jr      c,BttNoBackend
                call    StCaps
                ld      (BTpCaps),a
                ld      hl,BTpName
                ld      b,FA_READ
                call    StOpen
                jr      c,BttFail
                ld      (BTpH),a
                ld      hl,BTpBuf
                ld      bc,8
                call    StRead
                ld      (BTpRead),bc
                ld      a,(BTpH)
                call    StClose
                ; fold the eight bytes into one number to check
                ld      hl,BTpBuf
                ld      b,8
                ld      c,0
BttSum:
                ld      a,(hl)
                add     a,c
                ld      c,a
                inc     hl
                djnz    BttSum
                ld      a,c
                ld      (BTpSum),a
                jr      BttDone
BttFail:
                ld      (BTpErr),a
                jr      BttDone
BttNoBackend:
                ld      a,$EE
                ld      (BTpErr),a
BttDone:
                ld      a,ST_RAM                ; leave RAM selected
                call    StSelect
                ret

BTpName:        defb    "TAPETEST",0
BTpH:           defb    0
BTpRead:        defw    0
BTpSum:         defb    0
BTpErr:         defb    0
BTpCaps:        defb    0
BTpBuf:         defs    8


; Settings: the round trip, and then whether the record actually
; reaches the behaviour it names. The round trip was always the easy
; half. The half that was missing is that SPEED, INVERT Y, LATTICE
; and KEY PTR were stored, displayed and persisted while nothing
; read them, so the panel and the machine could disagree and no test
; would notice. These checks read the things SetApply writes:
; AccelPtr, and the six lattice operands it patches.
;
; BSetRes counts checks passed out of BSETN, like NOTE and KEYS. The
; old bitmask ran out of room at eight.
BSETN           equ     12

; ------------------------------------------------------------
;  Subjects that need no interrupt and no screen
;  Pure RAM, and zxtest.py drives them with an exit status. The
;  Fuse report showed the same counts as numbers to read off a
;  screenshot, which is the weaker of the two and was the copy
;  costing bytes in the build that had 581 left. The tape subject
;  is not among them: it calls the ROM loader, and the headless
;  emulator has no tape.
; ------------------------------------------------------------
IFDEF TEST
BSetOk:
                ld      hl,BSetRes
                inc     (hl)
                ret

BSetTest:
                xor     a
                ld      (BSetRes),a
                call    StInit
                call    SetDefaults
                ld      a,2
                ld      (SetSpeed),a
                ld      a,1
                ld      (SetInvertY),a
                ld      a,2
                ld      (SetKeyPtr),a
                call    SetSave
                call    nc,BSetOk                       ; 1 saved
                call    SetDefaults                     ; wipe it in memory
                call    SetLoad
                ld      a,(SetSpeed)
                cp      2
                call    z,BSetOk                        ; 2 speed survived
                ld      a,(SetInvertY)
                cp      1
                call    z,BSetOk                        ; 3 invert survived
                ld      a,(SetKeyPtr)
                cp      2
                call    z,BSetOk                        ; 4 key pointer survived

                ; a name that was never written must not be believed
                ld      hl,BSetMiss
                ld      b,FA_READ
                call    StOpen
                jr      nc,BstNoMiss
                call    SetDefaults
                ld      a,(SetSpeed)
                cp      1                               ; the default
                call    z,BSetOk                        ; 5 fell back
BstNoMiss:
                ; A record from an older build, under the right name, on
                ; the right backend, of the right length. Only the version
                ; byte is wrong, and that alone must be enough to refuse
                ; it. This is the check the version field exists for and
                ; it had never been run.
                ld      hl,SetName
                ld      b,FA_OVERWRITE
                call    StOpen
                jr      c,BstNoOld
                ld      (BSetH),a
                ld      hl,BSetOldRec
                ld      bc,SETSIZE
                call    StWrite
                ld      a,(BSetH)
                call    StClose
                call    SetLoad
                jr      nc,BstNoOld                     ; carry means refused
                ld      a,(SetSpeed)
                cp      1                               ; default, not the file's 2
                call    z,BSetOk                        ; 6 old version refused
BstNoOld:
                ; SPEED must select a ramp, not merely be stored
                call    SetDefaults
                xor     a
                ld      (SetSpeed),a
                call    SetApply
                ld      hl,(AccelPtr)
                ld      de,AccelTabs
                or      a
                sbc     hl,de
                ld      a,h
                or      l
                call    z,BSetOk                        ; 7 slow ramp selected

                ld      a,2
                ld      (SetSpeed),a
                call    SetApply
                ld      hl,(AccelPtr)
                ld      de,AccelTabs+ACCELLEN*2
                or      a
                sbc     hl,de
                ld      a,h
                or      l
                call    z,BSetOk                        ; 8 fast ramp selected

                ld      a,9                             ; out of range
                ld      (SetSpeed),a
                call    SetApply
                ld      a,(SetSpeed)
                cp      1
                jr      nz,BstNoClamp
                ld      hl,(AccelPtr)
                ld      de,AccelTabs+ACCELLEN
                or      a
                sbc     hl,de
                ld      a,h
                or      l
                call    z,BSetOk                        ; 9 corrupt speed clamped
BstNoClamp:
                ; DESKTOP must reach all four operands, in both routines
                ; that paint the lattice. The narrow column fill is the
                ; one that is easy to forget. The initial paint used to be
                ; a third copy; it goes through DevFillDesk now.
                xor     a
                ld      (SetLattice),a
                call    SetApply
                ld      a,(LatA1)
                ld      hl,LatA2
                or      (hl)
                ld      hl,LatB1
                or      (hl)
                ld      hl,LatB2
                or      (hl)
                call    z,BSetOk                        ; 10 plain desktop

                ld      a,1
                ld      (SetLattice),a
                call    SetApply
                ld      a,(LatA1)
                cp      $88
                jr      nz,BstNoLat
                ld      a,(LatA2)
                cp      $88
                jr      nz,BstNoLat
                ld      a,(LatB1)
                cp      $22
                jr      nz,BstNoLat
                ld      a,(LatB2)
                cp      $22
                call    z,BSetOk                        ; 11 dot lattice back
BstNoLat:
                ld      a,9                             ; out of range
                ld      (SetKeyPtr),a
                call    SetApply
                ld      a,(SetKeyPtr)
                cp      1
                call    z,BSetOk                        ; 12 corrupt gate clamped

                call    SetDefaults                     ; leave the machine tidy
                call    SetApply
                ret

BSetOldRec:     defb    SETMAGIC, 1, 2, 1, 0, 0, 0, 0
BSetH:          defb    0

BSetMiss:       defb    "NOSUCH",0
BSetRes:        defb    0
ENDIF

; ------------------------------------------------------------
;  D2: a panel with a field in it
;  The settings panel has no PT_FIELD, so without this the field
;  would be code that had never been run, and this project does
;  not commit that. It is also the shape G3 needs: a name to type
;  and a button to press.
;
;  The field is on its own row with no label. Ten usable columns
;  cannot hold a label and a name as well, and a name box
;  narrower than the name it holds is worse than a label. Nine
;  characters and not ten, because the cursor needs a column of
;  its own when the buffer is full; a real tape name needs a
;  wider panel than the save under buffer can carry.
; ------------------------------------------------------------
IFDEF TEST              ; driven only from zxtest.py, so the Fuse
                        ; harness does not carry it
BPNLNAME        equ     10              ; a tape name, which B3 made room for

BPnlSetup:
                ld      a,DLGX
                ld      (PnlX),a
                ld      a,DLGY
                ld      (PnlY),a
                ld      a,DLGW
                ld      (PnlW),a
                ld      a,DLGH
                ld      (PnlH),a
                ld      a,DLGROW0
                ld      (PnlRow0),a
                ld      a,3
                ld      (PnlRows),a
                ld      hl,BPnlRows
                ld      (PnlTab),hl
                ld      hl,BPnlTitle
                ld      (PnlTitle),hl
                ld      hl,0
                ld      (PnlAfter),hl
                ld      a,1                     ; the focus starts on the field
                ld      (PnlFocus),a
                xor     a
                ld      (PnlFCur),a
                ld      (BPnlBuf),a             ; and the name starts empty
                ld      (BPnlHit),a
                ret

BPnlOk:
                ld      a,1
                ld      (BPnlHit),a
                ret

BPnlRows:
                defb    PT_LABEL
                defw    BPnlLbl, 0
                defb    0
                defw    0

                defb    PT_FIELD
                defw    0, BPnlBuf
                defb    BPNLNAME
                defw    0

                defb    PT_ACTION
                defw    BPnlOkTxt, BPnlOk
                defb    0
                defw    0

BPnlTitle:      defb    "SAVE AS",0
BPnlLbl:        defb    "NAME",0
BPnlOkTxt:      defb    "OK",0
BPnlHit:        defb    0
BPnlBuf:        defs    BPNLNAME+1
ENDIF

IFDEF TEST
                include "kbdtest.inc"
ENDIF

ENDIF
