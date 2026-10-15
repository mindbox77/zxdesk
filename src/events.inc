; ============================================================
;  Event queue
;  MainLoop used to be a fixed sequence of calls with the window
;  drag logic wired straight into it. Everything now goes through
;  a ring of events, so a new control or application registers a
;  handler instead of being spliced into the loop.
;
;  Sixteen slots of four bytes. Posting to a full queue drops the
;  event silently, which is the right failure here: a lost pointer
;  move is invisible on the next frame, a stalled loop is not.
;
;  EvPoll runs at the end of a frame, after ReadInput, and turns
;  changes in the raw input state into events. Dispatch happens in
;  the same frame, so the window position is settled before the
;  next frame's paint, which is what keeps PREWORK constant.
; ============================================================
EVSLOTS         equ     16
EVMASK          equ     EVSLOTS-1

EV_PTRMOVE      equ     1               ; B = PtrX, C = PtrY
EV_BTNDOWN      equ     2               ; B = button mask
EV_BTNUP        equ     3
EV_KEY          equ     4               ; B = key code
EV_TYPES        equ     4

; A = type, B and C = parameters. Carry set if it was dropped.
EvPost:
                push    bc
                push    af
                ld      a,(EvHead)
                inc     a
                and     EVMASK
                ld      hl,EvTail
                cp      (hl)
                jr      z,EvpFull
                ld      e,a
                ld      a,(EvHead)
                ld      l,a
                ld      h,0
                add     hl,hl
                add     hl,hl
                ld      bc,EvQueue
                add     hl,bc
                pop     af
                ld      (hl),a
                inc     hl
                pop     bc
                ld      (hl),b
                inc     hl
                ld      (hl),c
                ld      a,e
                ld      (EvHead),a
                or      a
                ret
EvpFull:
                pop     af
                pop     bc
                scf
                ret

; Returns A = type, or zero when the queue is empty. B and C carry
; the parameters.
EvNext:
                ld      a,(EvTail)
                ld      hl,EvHead
                cp      (hl)
                jr      z,EvnEmpty
                ld      l,a
                ld      h,0
                add     hl,hl
                add     hl,hl
                ld      bc,EvQueue
                add     hl,bc
                ld      a,(EvTail)
                inc     a
                and     EVMASK
                ld      (EvTail),a
                ld      a,(hl)
                inc     hl
                ld      b,(hl)
                inc     hl
                ld      c,(hl)
                ret
EvnEmpty:
                xor     a
                ret

; Drain the queue. Handlers receive B and C and may post further
; events, which are picked up on the next turn of this loop.
EvDispatch:
                call    EvNext
                or      a
                ret     z
                cp      EV_TYPES+1
                jr      nc,EvDispatch   ; unknown type, discard
                dec     a
                add     a,a
                ld      l,a
                ld      h,0
                ld      de,EvHandlers
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ex      de,hl
                call    EvCallHL
                jr      EvDispatch
EvCallHL:
                jp      (hl)

EvHandlers:
                defw    HdlPtrMove
                defw    HdlBtnDown
                defw    HdlBtnUp
                defw    HdlKey
EvIgnore:
                ret

; B = key code. The menu and the settings panel are modal in the same
; way they are for a press: while either is up it owns the keyboard,
; and the notepad hears nothing. The panel does something with it now
; rather than dropping it. Otherwise the note is the only thing with a
; text cursor, so it takes every key without a hit test.
HdlKey:
                ld      a,(DlgUp)
                or      a
                jr      nz,HkDlg
                ld      a,(MenuOpen)
                or      a
                ret     nz
                ; A shortcut is taken here, before a window is selected and
                ; before any application is offered it. The codes are $81
                ; and up, which no text table can produce, so this cannot
                ; mistake a letter for a shortcut and an application cannot
                ; be handed one by accident. A dialogue or an open menu has
                ; already returned above, which is what keeps a modal panel
                ; modal.
                ld      a,b
                cp      SC_BASE
                jp      nc,ScDispatch
                ; B4. The key goes to the window at the front of the z
                ; order, because focus is the front window rather than a
                ; separate variable. The about window has nothing to type
                ; into, so it drops it rather than passing it to a notepad
                ; that is behind something.
                ld      a,(WndCount)
                or      a
                ret     z                       ; nothing to type into
                push    bc
                ld      a,(WndZ)
                call    WndSelect
                pop     bc
                ; F2. The key goes to whatever owns the front window,
                ; through its descriptor. The about window's vector is
                ; null, so it still drops what is typed at it, but this
                ; routine no longer has to know that there is an about
                ; window or how many kinds of window there are.
                ld      a,b
                ld      c,APP_KEY
                jp      AppCall
HkDlg:
                ; D2 gave the panel a focus bar, so the keyboard has
                ; somewhere to point and the panel can be driven without a
                ; mouse. That matters on a machine most of whose owners had
                ; no mouse, and it is what stops the mouse presence
                ; heuristic being load bearing.
                ld      a,b
                jp      DlgKey

; Turn raw input state into events. Runs after ReadInput, at the end
; of a frame. Only the left button is reported for now; the right one
; has nothing to open yet.
EvPoll:
                ld      a,(EvLastBtn)
                ld      c,a
                ld      a,(Buttons)
                ld      (EvLastBtn),a
                xor     c
                bit     1,a             ; did the left button change?
                jr      z,EvpNoBtn
                ld      a,(Buttons)
                ld      b,a
                bit     1,a             ; active low, so zero is pressed
                ld      a,EV_BTNDOWN
                jr      z,EvpBtnGo
                ld      a,EV_BTNUP
EvpBtnGo:
                ld      c,0
                call    EvPost
EvpNoBtn:
                ld      a,(PtrX)
                ld      b,a
                ld      a,(EvLastX)
                cp      b
                jr      nz,EvpMoved
                ld      a,(PtrY)
                ld      b,a
                ld      a,(EvLastY)
                cp      b
                jp      z,KbdPoll
EvpMoved:
                ld      a,(PtrX)
                ld      (EvLastX),a
                ld      b,a
                ld      a,(PtrY)
                ld      (EvLastY),a
                ld      c,a
                ld      a,EV_PTRMOVE
                call    EvPost
                jp      KbdPoll

EvHead:         defb    0
EvTail:         defb    0
EvLastBtn:      defb    $FF             ; nothing pressed, active low
EvLastX:        defb    120
EvLastY:        defb    90
EvQueue:        defs    EVSLOTS*4
