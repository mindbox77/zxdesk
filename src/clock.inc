; ============================================================
;  Clock
;  There is no real time clock on a 48K and there is no battery,
;  so this is not a clock that knows the time. It is a clock that
;  counts, set by hand, and the thing it counts is the only
;  reliable interval the machine has: the ULA interrupt.
;
;  It counts IrqCnt rather than its own frames, which is A3's
;  counter and was built to notice dropped frames. Reading the
;  difference since the last look means a frame the main loop
;  missed still advances the clock, because the interrupt
;  happened whether or not anything was listening. A clock that
;  loses time whenever a window is dragged would be a worse clock
;  than one that is merely set by hand.
;
;  THE INTERRUPT IS NOT 50 HZ. The 48K frame is exactly 69,888 T
;  and the clock is 3.5 MHz, so the rate is 3500000/69888 =
;  50.0801 Hz. Ticking a second every fifty interrupts makes the
;  clock gain 0.16%, which is 2 minutes 18 seconds a day, and
;  that is bad enough to notice within a week.
;
;  So a second is fifty interrupts, and every so often fifty one.
;  A hundredth is accumulated each second and an extra interrupt
;  is demanded when it carries: 0.08 against a true 0.08013 on a
;  48K. On a 128K the frame is 70,908 T at 3.5469 MHz, so the
;  rate is 50.0211 Hz and the addend is 2 rather than 8.
;
;  Driven for an hour in the headless harness, this clock takes
;  180,287 interrupts to reach 13:00 from 12:00. The true figure
;  is 3600 x 3500000 / 69888 = 180,288, so it gains one interrupt
;  an hour, which is half a second a day. A naive fifty would
;  take 180,000 and gain two minutes eighteen. That is measured
;  rather than derived, which is the difference between this
;  paragraph and the one above it.
;
;  Both figures are arithmetic from the frame period, and the
;  frame period is the one number on this machine that has been
;  measured rather than assumed: docs/measured-2026-08-29.md.
;  The crystal itself is nominal, so the real error on real
;  hardware is whatever the crystal is, and no software here can
;  know that.
;
;  The time is the machine's rather than the window's, so the
;  application keeps no per instance state and two clock windows
;  show the same time. That is not a limitation worked around, it
;  is what a clock is.
; ============================================================
; Hours and minutes. Seconds were shown at first and they were
; uneconomical in the exact sense: the display changed sixty times a
; minute, and every change is a PrintStrPx2 and a WinGrab, about
; 31,000 T. That is 0.9% of the machine spent telling you something
; you were not reading. The seconds are still counted, because a
; minute cannot roll without them; they are simply not painted, and
; the window repaints sixty times less often.
CLKW            equ     8
CLKH            equ     30
CLKFRAC48       equ     8               ; hundredths of an interrupt a second
CLKFRAC128      equ     2

; ------------------------------------------------------------
;  ClkInit
;  Noon, because midnight is indistinguishable from unset and a
;  clock reading 00:00:00 looks broken rather than unset.
; ------------------------------------------------------------
ClkInit:
                ld      a,12
                ld      (ClkH),a
                xor     a
                ld      (ClkM),a
                ld      (ClkS),a
                ld      (ClkFrames),a
                ld      (ClkFrac),a
                ld      (ClkField),a
                ld      a,50
                ld      (ClkNeed),a
                ld      hl,(IrqCnt)
                ld      (ClkLast),hl
                ; DetectMachine has already run, so which machine this is
                ; is known and the correction is the right one from the
                ; first second rather than after a fix up
                ld      a,(Is128)
                or      a
                ld      a,CLKFRAC48
                jr      z,ClkInitFrac
                ld      a,CLKFRAC128
ClkInitFrac:
                ld      (ClkFracAdd),a
                ret

; ------------------------------------------------------------
;  ClkTick
;  Called once a frame. Advances by however many interrupts have
;  happened since the last look, which is normally one.
; ------------------------------------------------------------
ClkTick:
                ld      hl,(IrqCnt)
                ld      de,(ClkLast)
                or      a
                sbc     hl,de
                ret     z                       ; the common case on a machine
                                                ; whose loop is keeping up is
                                                ; that this is one, not nought,
                                                ; but nought happens the first
                                                ; time round
                ld      (ClkDelta),hl
                ld      hl,(IrqCnt)
                ld      (ClkLast),hl
                ld      hl,(ClkDelta)
ClkAccum:
                ld      a,(ClkFrames)
                inc     a
                ld      (ClkFrames),a
                ld      b,a
                ld      a,(ClkNeed)
                cp      b
                jr      nz,ClkNoSec
                push    hl
                call    ClkSecond
                pop     hl
ClkNoSec:
                dec     hl
                ld      a,h
                or      l
                jr      nz,ClkAccum
                ret

; One second. Works out how many interrupts the next one needs before
; it advances anything, because the correction belongs to the second
; that is starting rather than the one that has ended.
ClkSecond:
                ld      hl,TickSecs             ; for AppTick
                inc     (hl)
                xor     a
                ld      (ClkFrames),a
                ld      a,(ClkFrac)
                ld      b,a
                ld      a,(ClkFracAdd)
                add     a,b
                cp      100
                jr      c,ClkNoExtra
                sub     100
                ld      (ClkFrac),a
                ld      a,51
                ld      (ClkNeed),a
                jr      ClkBump
ClkNoExtra:
                ld      (ClkFrac),a
                ld      a,50
                ld      (ClkNeed),a
ClkBump:
                ld      hl,ClkS
                inc     (hl)
                ld      a,(hl)
                cp      60
                ret     c                       ; no repaint: the seconds are
                                                ; counted and not shown
                ld      (hl),0
                ld      a,1                     ; the minute is what the face
                ld      (ClkDirty),a            ; actually says
                ld      hl,ClkM
                inc     (hl)
                ld      a,(hl)
                cp      60
                ret     c
                ld      (hl),0
                ld      hl,ClkH
                inc     (hl)
                ld      a,(hl)
                cp      24
                ret     c
                ld      (hl),0
                jp      CalNextDay              ; midnight is the one moment
                                                ; the date is not the user's
                                                ; to remember

; ------------------------------------------------------------
;  ClkService
;  Called from the main loop. Ticks, and repaints if a second has
;  passed and a clock is the window in front.
;
;  Only when it is in front, and that is a real limitation rather
;  than an oversight: WinGrab reads the screen, so re-grabbing a
;  window that something else overlaps would copy the other
;  window's pixels into this one's buffer. A clock behind another
;  window therefore stops until it is raised, and raising it
;  repaints it with the time it is by then.
; ------------------------------------------------------------
ClkService:
                call    ClkTick
                ld      a,(ClkDirty)
                or      a
                ret     z
                xor     a
                ld      (ClkDirty),a
                ld      hl,(WinApp)
                ld      de,AppClock
                or      a
                sbc     hl,de
                ret     nz                      ; the front window is not one
                call    ApiWndIsFront
                ret     nz
                call    ApiPtrRestore
                call    ClkDraw
                call    ApiWinGrab
                call    ApiPtrSaveBg
                jp      ApiPtrDraw

; ------------------------------------------------------------
;  ClkDraw
;  F2's draw vector. The whole time in one PrintStrPx2, then the
;  field the keys would change printed again inverted, because in
;  a one bit display inversion is the only emphasis there is and
;  the system already spends it on chrome.
; ------------------------------------------------------------
ClkDraw:
                ld      hl,ClkText
                ld      a,(ClkH)
                call    ClkPut2
                ld      (hl),':'
                inc     hl
                ld      a,(ClkM)
                call    ClkPut2
                ld      (hl),0
                ld      bc,CLKROW*256           ; row CLKROW, column 0
                ld      hl,ClkText
                call    ApiWinPrint
                ; and the selected field over the top of itself
                ld      a,(ClkField)
                ld      b,a
                add     a,a
                add     a,b                     ; three columns to a field
                ld      e,a
                ld      d,0
                ld      hl,ClkText
                add     hl,de
                ld      a,(hl)
                ld      (ClkPair),a
                inc     hl
                ld      a,(hl)
                ld      (ClkPair+1),a
                ld      c,e
                ld      b,CLKROW
                ld      hl,ClkPair
                jp      ApiWinPrintInv

; A = a value below 100, HL = where its two digits go, and HL is left
; after them.
ClkPut2:
                ld      b,'0'-1
ClkP2T:
                inc     b
                sub     10
                jr      nc,ClkP2T
                add     a,10
                ld      (hl),b
                inc     hl
                add     a,'0'
                ld      (hl),a
                inc     hl
                ret

; ------------------------------------------------------------
;  ClkKey
;  F2's key vector. Left and right choose a field, up and down
;  change it. There is nothing else a clock with no radio and no
;  battery can be asked.
; ------------------------------------------------------------
ClkKey:
                cp      KEY_LEFT
                jr      nz,CkNotL
                ld      a,(ClkField)
                or      a
                jr      z,CkPaint
                dec     a
                ld      (ClkField),a
                jr      CkPaint
CkNotL:
                cp      KEY_RIGHT
                jr      nz,CkNotR
                ld      a,(ClkField)
                cp      1
                jr      nc,CkPaint
                inc     a
                ld      (ClkField),a
                jr      CkPaint
CkNotR:
                cp      KEY_UP
                jr      nz,CkNotU
                ld      c,1
                call    ClkAdjust
                jr      CkPaint
CkNotU:
                cp      KEY_DOWN
                ret     nz
                ld      c,0
                call    ClkAdjust
CkPaint:
                call    ApiPtrRestore
                call    ClkDraw
                call    ApiWinGrab
                call    ApiPtrSaveBg
                jp      ApiPtrDraw

; C = 1 up, 0 down. Wraps within the field rather than carrying into
; the next one: setting the minutes should not move the hour under
; the hand of whoever is setting it.
ClkAdjust:
                ld      a,(ClkField)
                ld      hl,ClkH
                or      a
                jr      z,CkaGot
                inc     hl
                dec     a
                jr      z,CkaGot
                inc     hl
CkaGot:
                ld      b,60
                ld      a,(ClkField)
                or      a
                jr      nz,CkaMod
                ld      b,24
CkaMod:
                ld      a,c
                or      a
                jr      z,CkaDown
                ld      a,(hl)
                inc     a
                cp      b
                jr      c,CkaSet
                xor     a
                jr      CkaSet
CkaDown:
                ld      a,(hl)
                or      a
                jr      nz,CkaDec
                ld      a,b
CkaDec:
                dec     a
CkaSet:
                ld      (hl),a
                xor     a                       ; the minute restarts from the
                ld      (ClkFrames),a           ; moment it was set, which is
                ld      (ClkS),a                ; what setting a clock means
                ret

ClkText:        defs    6
ClkPair:        defb    0,0,0
CLKROW          equ     4               ; rows below the title bar

ClkH:           defb    12
ClkM:           defb    0
ClkS:           defb    0
ClkField:       defb    0
ClkFrames:      defb    0
ClkNeed:        defb    50
ClkFrac:        defb    0
ClkFracAdd:     defb    CLKFRAC48
ClkDirty:       defb    0
ClkVal:         defb    0
ClkCol:         defb    0
ClkLast:        defw    0
ClkDelta:       defw    0
