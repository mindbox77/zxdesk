; ============================================================
;  ZX DESK
;  Stage 1: monochrome surface, screen row table, Kempston
;           mouse driver and a pointer with save under.
;
;  Assembler: pasmo
;    pasmo --tapbas zxdesk.asm zxdesk.tap
;
;  Colour model is monochrome by design. Every attribute cell
;  holds the same value, so the pointer and any future window
;  can sit at any pixel position with no clash at all.
; ============================================================

SCREEN          equ     $4000
ATTRS           equ     $5800
CHARSET         equ     $3C00           ; ROM font, addr = CHARSET + char*8
ULA             equ     254
PAGEPORT        equ     $7FFD

ATTR_MONO       equ     $78             ; bright, white paper, black ink

; The extent of the display, said once. These were bare literals in
; six files until the H1 audit went looking for every place that
; assumes a 32 column screen and found that looking was the only way
; to find them. pasmo folds them, so naming them costs nothing at run
; time and turns that search into a grep.
SCRCOLS         equ     32              ; byte columns across the screen
SCRROWS         equ     192             ; pixel rows down it

MENUH           equ     9               ; menu bar rows 0..8, rule on 8
DESKTOP_TOP     equ     9
DESKTOP_BOT     equ     182
STATRULE        equ     183
STATROW         equ     23              ; char row for the status line

; The program takes the machine over completely, so it uses its own
; stack and its own interrupt handler rather than inheriting BASIC's
; stack and depending on the ROM handler and IY being intact.
; All three live in the fixed $8000..$BFFF region so that paging the
; $C000 bank during machine detection cannot pull them away.
STACKTOP        equ     $BD00
IRQHANDLER      equ     $BDBD
IRQTABLE        equ     $BE00

PTRH            equ     11              ; pointer height in pixels
PTRXMAX         equ     247             ; keeps the two byte span on screen
PTRYMAX         equ     172             ; keeps the pointer clear of the status band
KBDSTEP         equ     3               ; pixels per frame on the keyboard
MOUSEMAX        equ     64              ; the most a mouse may move in one frame

; ------------------------------------------------------------
;  The slow region
;  Code has to live below $C000, because $C000 upwards is the
;  bank F3 pages, and the stack is at $BD00. That is 15,616
;  bytes and the desktop had grown to 14,940 of it, which left
;  the timing harness 676 bytes to fit 2,000 in. The bench is
;  the only thing on this project that has ever measured the
;  ULA, so losing it was not an option.
;
;  $5CB6 to $7FFF has been empty since the first build and the
;  state document has been calling it 8,522 free bytes for
;  months. It is contended: the ULA steals cycles there while
;  the display is being painted, so code in it runs perhaps a
;  third slower. That is the whole of the cost, and it is only
;  a cost for code that runs while the beam is on the screen.
;
;  So the rule for this region is one line long: nothing here
;  may run inside a frame. The applications paint when a window
;  opens or a key arrives, the file manager paints when a menu
;  item is picked, and none of them is on the drag path or the
;  pointer path or in the interrupt. Anything that is stays at
;  $8000 where the bench can measure it honestly.
;
;  It starts at $6000 rather than $5CB6 because the BASIC loader
;  the tape carries lives at PROG, which is $5CCB, and a CODE
;  block that overwrote the program doing the loading would be a
;  novel way to fail. $6000 leaves it a comfortable page.
; ------------------------------------------------------------
SLOWORG         equ     $6000
SLOWTOP         equ     $8000

                org     32768

; ------------------------------------------------------------
;  Entry. Main is slot 0 of the table in api.inc.
; ------------------------------------------------------------
Main:
                jp      Start
                include "api.inc"
Start:
IFDEF BENCH
                jp      BenchMain       ; bench build only, emits nothing otherwise
ENDIF
IFDEF MOUSETEST
                jp      MouseTestMain   ; the Kempston mouse diagnostic
ENDIF
IFDEF ESXTEST
                jp      EsxTestMain     ; the esxDOS RST 8 probe
ENDIF
                di
                ld      sp,STACKTOP
                call    DetectMachine
                call    SetupIM2
                call    BuildScrTab
                call    MouseInit
                call    JoyInit
                call    StInit
                call    SetLoad                 ; carry means defaults, which
                call    SetApply                ; SetLoad has already put back
                call    DskInit         ; the shortcuts a fresh machine has,
                call    DskLoad         ; then whatever was arranged and saved
                                        ; if anything ever was, and both
                                        ; before InitScreen, which paints them
                call    InitScreen              ; after SetApply, so the first
                                                ; desktop is painted with the
                                                ; saved lattice rather than
                                                ; the default and a repaint
                ; NoteNew used to be called here, because the document was
                ; a global that had to exist before anything drew it. It is
                ; window nought's instance now and WndInit makes it through
                ; the application's own init vector.
                call    WndInit
IFDEF PRINTDEMO
                ; Two lines in the document, so the print demo has something
                ; to print. The script drives the pointer and cannot type.
                ld      hl,TxtPrintDemo
                ld      de,NoteBuf
                ld      bc,32
                ldir
ENDIF
                call    ClkInit         ; after DetectMachine, so the interrupt
                                        ; rate it corrects for is this
                                        ; machine's rather than a 48K's
                call    WinDraw
                call    WinGrab         ; compose once, copy thereafter
                call    PtrSaveBg
                call    PtrDraw
                ei
MainLoop:
                halt                    ; wake at the start of the top border
                call    FrameWatch      ; heartbeat, so a freeze is visible,
                                        ; and A3's overrun count

                ; Everything that touches the pointer happens here, in the
                ; top border, before the ULA starts painting the display
                ; about 14336 T states after the interrupt. Erasing now and
                ; redrawing after the beam had already passed was why the
                ; pointer was invisible in the upper part of the screen.
                ; The position was worked out at the end of the previous
                ; frame, so nothing has to be computed first.
                call    PtrRestore
                call    PtrSaveBg
                call    PtrDraw

                ; The window position was settled at the end of the previous
                ; frame. If it moved, wait for the beam to finish painting
                ; those rows before touching them, then redraw into the gap
                ; behind it. The redraw runs past the interrupt by design,
                ; which is what Rule B allows, so a drag frame is a 25 Hz
                ; frame until damage rectangles cut the redraw down.
                ld      a,(WinMoved)
                or      a
                jr      z,MlIdle
IFNDEF NOWAIT
                call    WaitBeamTopOfWin
ENDIF
                call    WinRedraw
                jr      MlInput
MlIdle:
                ; Nothing moved, so there is room for the debug status line.
                ; The scripted build wants the status row for its own report.
                ; The clock counts whatever happens and repaints only when
                ; nothing is being dragged: a resize outline and a window
                ; redraw both want the whole of an idle frame more than a
                ; clock wants to be one second fresher.
                ld      a,(Dragging)
                ld      hl,Resizing
                or      (hl)
                call    z,ClkService
IFNDEF SCRIPT
                call    ShowStatus      ; cheap enough to run during a drag
ENDIF
MlInput:
                ; Input is read and dispatched at the end of the frame. That
                ; leaves the model settled before the next frame paints it,
                ; and keeps everything before WaitBeamTopOfWin fixed in cost,
                ; which is what makes PREWORK a constant. MainLoop knows
                ; nothing about windows: the handlers do.
IFDEF DEMO
                call    DemoStep
ELSE
IFDEF SCRIPT
                call    ScriptStep
ELSE
                call    ReadInput
                call    EvPoll
                call    EvDispatch
ENDIF
ENDIF
                jr      MainLoop

; ------------------------------------------------------------
;  Machine detection
;  Writes a marker to bank 0 at $C000, pages bank 1 in, writes a
;  different marker, pages bank 0 back and looks for the first
;  marker. On a 48K the out does nothing so the second write is
;  still there. Bit 4 is held high throughout so the 48K ROM
;  stays paged and the character set remains readable.
; ------------------------------------------------------------
DetectMachine:
                ld      a,1
                ld      (Is128),a
                ld      bc,PAGEPORT
                ld      a,$10           ; ROM 1, screen 5, bank 0
                out     (c),a
                ld      hl,$C000
                ld      (hl),$AA
                ld      a,$11           ; bank 1
                out     (c),a
                ld      (hl),$55
                ld      a,$10           ; bank 0 again
                out     (c),a
                ld      (PageCur),a     ; F3 shadows this port, because it is
                                        ; write only and cannot be read back
                ld      a,(hl)
                cp      $AA
                ret     z
                xor     a
                ld      (Is128),a
                ret

; ------------------------------------------------------------
;  Interrupts
;  A 257 byte table of $BD gives a vector of $BDBD whatever the
;  bus happens to put on the data lines, and the handler there
;  does nothing but re-enable and return. Nothing in the ROM is
;  involved, so IY and the system variables are irrelevant.
;
;  The table looks like an obvious 257 bytes to save, and it is
;  not. The trick that removes it, which demo code uses and which
;  was proposed here, is to point I at a page of the ROM that
;  holds nothing but $FF: both vector bytes then read $FF whatever
;  the bus does, the handler address is $FFFF, and four bytes of
;  RAM at the top of memory turn that into a jump. It was measured
;  against the ROM images rather than argued about, and it fails
;  twice.
;
;  First, the $FF run is a property of the 48K ROM alone. In
;  48.rom it is $386E to $3CFF, 1170 bytes, which makes I = $39,
;  $3A or $3B safe and nothing else: I = $38 would read its vector
;  from $3800, which is code. But the 128K's ROM 1 is not the 48K
;  ROM. It differs in 1177 bytes and it spends that filler tail on
;  code, so the longest run of $FF anywhere in its $3800 page is
;  two bytes. There is no safe I on a 128K, and the same is true
;  of the +2. Paging ROM 1 does not rescue it.
;
;  Second, and on its own enough: on a 128K the four bytes at
;  $FFF4 and $FFFF are in the bank that $C000 pages, not in fixed
;  RAM. bank.inc depends on the handler living in bank 2 so that a
;  transfer can page without turning interrupts off, and banks 3,
;  4, 6 and 7 hold file chunks that run all the way to $FFFF. The
;  stub would sit on file data and vanish when the bank changed.
;
;  The saving would not have been what it looked like either.
;  IRQTABLE is an equ filled by the LDIR below, not a DEFS, so it
;  costs no tape at all: the change is 31 bytes of code down to
;  23. What it would really buy is the 768 bytes of fixed region
;  between STACKTOP and BUFBASE, and only in a 48K-only binary,
;  which this is not.
; ------------------------------------------------------------
SetupIM2:
                ld      hl,IRQTABLE
                ld      de,IRQTABLE+1
                ld      bc,256
                ld      (hl),$BD
                ldir
                ld      a,$C3           ; JP IrqTick. The handler used to be
                ld      (IRQHANDLER),a  ; EI then RET in place; A3 gives it work
                ld      hl,IrqTick      ; to do, and the work does not fit here
                ld      (IRQHANDLER+1),hl
                ld      a,IRQTABLE/256
                ld      i,a
                im      2
                ret

; ------------------------------------------------------------
;  A3: the frame watchdog
;  IrqTick counts every interrupt the machine takes. FrameWatch
;  counts every frame the main loop actually reached the top of.
;  They diverge exactly when a frame's work ran past the
;  interrupt, and the difference is the number of frames dropped.
;
;  The handler is the awkward half. It fires in the middle of a
;  fill, where the stack is not a stack, so it cannot push; where
;  AF' carries DevFillDesk's lattice pattern from row to row and
;  the alternate set holds DevFillRect's row pointer, so it cannot
;  use EX AF,AF' or EXX either. IX is the one pair it can borrow,
;  because it can be saved and restored through memory.
;
;  It cannot touch the flags either, and the first version did.
;  INC (IX+0) sets them, and a fill holds a live carry across the
;  ADD HL,DE that finds the end of a row and the JR C that decides
;  whether the row crossed a third boundary. The interrupt sweep
;  caught it immediately, as two wrong pixels, which is what an
;  interrupt landing in that gap and stealing the carry looks
;  like. INC IX is a sixteen bit increment and sixteen bit
;  increments leave the flags alone, so the counter is a word and
;  the handler is transparent. About 104 T fifty times a second,
;  which is 0.15 per cent of the machine.
; ------------------------------------------------------------
IrqTick:
                ld      (IrqSaveIX),ix
                ld      ix,(IrqCnt)
                inc     ix              ; sets no flag, which is the point
                ld      (IrqCnt),ix
                ld      ix,(IrqSaveIX)
                ei
                ret

FrameWatch:
                ld      hl,FrameCnt
                inc     (hl)
                ld      a,(IrqCnt)
                sub     (hl)
                ret     z                       ; on time, and this is the
                                                ; common case, so it is 46 T
                ld      e,a                     ; interrupts the loop never saw
                ld      d,0
                ld      hl,(Dropped)
                add     hl,de
                jr      c,FwSaturate
                ld      (Dropped),hl
FwSync:
                ld      a,(IrqCnt)
                ld      (FrameCnt),a            ; resync, so the next report is
                ret                             ; about the next frame alone
FwSaturate:
                ld      hl,$FFFF                ; a wrap would read as recovery
                ld      (Dropped),hl
                jr      FwSync

; ------------------------------------------------------------
;  Screen addressing
;  A 192 entry table costs 384 bytes and turns every row lookup
;  into an index instead of the usual bit shuffling.
; ------------------------------------------------------------
BuildScrTab:
                ld      hl,ScrTab
                xor     a
                ld      (TmpY),a
BstLoop:
                ld      a,(TmpY)
                call    CalcScr         ; DE = address, column 0
                ld      (hl),e
                inc     hl
                ld      (hl),d
                inc     hl
                ld      a,(TmpY)
                inc     a
                ld      (TmpY),a
                cp      SCRROWS
                jr      nz,BstLoop
                ret

; in: A = pixel row, out: DE = screen address of column 0
CalcScr:
                ld      d,a
                and     %00000111
                ld      e,a
                ld      a,d
                and     %00111000
                rlca
                rlca
                ld      c,a
                ld      a,d
                and     %11000000
                rrca
                rrca
                rrca
                or      e
                or      %01000000
                ld      d,a
                ld      e,c
                ret

; in: A = pixel row, out: HL = screen address of column 0
RowAddr:
                ld      l,a
                ld      h,0
                add     hl,hl
                ld      de,ScrTab
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ex      de,hl
                ret

; Step HL down one pixel row. Touches only A and HL, which is what
; lets the pointer loops keep DE and BC live across it.
ScrDown:
                inc     h
                ld      a,h
                and     7
                ret     nz
                ld      a,l
                add     a,SCRCOLS
                ld      l,a
                ret     c               ; carried into the next third
                ld      a,h
                sub     8
                ld      h,a
                ret

; in: A = pixel row, C = byte column, out: HL = screen address
; Destroys A, DE and HL.
AddrAt:
                call    RowAddr
                ld      e,c
                ld      d,0
                add     hl,de
                ret

; ------------------------------------------------------------
;  Surface
; ------------------------------------------------------------
InitScreen:
                ld      hl,ATTRS        ; one attribute value everywhere
                ld      de,ATTRS+1
                ld      bc,767
                ld      (hl),ATTR_MONO
                ldir
                ld      a,7             ; white border
                out     (ULA),a
                ld      hl,SCREEN
                ld      de,SCREEN+1
                ld      bc,6143
                ld      (hl),0
                ldir

                call    DrawDesktop
                ld      a,MENUH-1
                call    FillRowSolid
                ld      a,STATRULE
                call    FillRowSolid

                ld      hl,TxtMenu
                ld      b,0
                ld      c,1
                call    PrintStr
                ret

; sparse dot lattice, four pixels apart, offset on alternate rows
DrawDesktop:
                ld      a,DESKTOP_TOP
DdLoop:
                ld      (TmpY),a
                and     3
                jr      z,DdPat0
                cp      2
                jr      z,DdPat2
                ld      c,0
                jr      DdFill
DdPat0:
                ld      c,$88
LatA3           equ     $-1             ; SetApply patches the operand
                jr      DdFill
DdPat2:
                ld      c,$22
LatB3           equ     $-1
DdFill:
                ld      a,(TmpY)
                call    RowAddr
                ld      b,SCRCOLS
DdRow:
                ld      (hl),c
                inc     l
                djnz    DdRow
                ld      a,(TmpY)
                inc     a
                cp      DESKTOP_BOT+1
                jr      nz,DdLoop
                jp      DskPaintAll             ; the shortcuts are part of
                                                ; what a repaint puts back,
                                                ; not something painted over
                                                ; it and then forgotten

; in: A = pixel row
FillRowSolid:
                call    RowAddr
                ld      b,SCRCOLS
FrsLoop:
                ld      (hl),$FF
                inc     l
                djnz    FrsLoop
                ret

; in: A = pixel row
ClearRow:
                call    RowAddr
                ld      b,SCRCOLS
ClrLoop:
                ld      (hl),0
                inc     l
                djnz    ClrLoop
                ret

; ------------------------------------------------------------
;  Text
; ------------------------------------------------------------
; in: A = char code, B = char row, C = char column
PrintChar:
                ld      l,a
                ld      h,0
                add     hl,hl
                add     hl,hl
                add     hl,hl
                ld      de,CHARSET
                add     hl,de
                push    hl
                ld      a,b
                add     a,a
                add     a,a
                add     a,a             ; char row to pixel row
                call    AddrAt
                pop     de
                ld      b,8
PcRow:
                ld      a,(de)
                ld      (hl),a
                inc     de
                inc     h
                djnz    PcRow
                ret

; in: HL = zero terminated string, B = char row, C = char column
PrintStr:
                ld      a,(hl)
                or      a
                ret     z
                push    hl
                push    bc
                call    PrintChar
                pop     bc
                pop     hl
                inc     hl
                inc     c
                jr      PrintStr

; in: A = value, B = char row, C = char column. Prints three digits.
PrintDec3:
                ld      (DecVal),a
                ld      d,0
Pd100:
                cp      100
                jr      c,Pd100d
                sub     100
                inc     d
                jr      Pd100
Pd100d:
                ld      (DecVal),a
                ld      a,d
                add     a,'0'
                push    bc
                call    PrintChar
                pop     bc
                inc     c
                ld      a,(DecVal)
                ld      d,0
Pd10:
                cp      10
                jr      c,Pd10d
                sub     10
                inc     d
                jr      Pd10
Pd10d:
                ld      (DecVal),a
                ld      a,d
                add     a,'0'
                push    bc
                call    PrintChar
                pop     bc
                inc     c
                ld      a,(DecVal)
                add     a,'0'
                push    bc
                call    PrintChar
                pop     bc
                ret

; ------------------------------------------------------------
;  Pointer
;  Save under, then AND the mask out and OR the image in, so the
;  white outline reads against both the dither and solid black.
; ------------------------------------------------------------
PtrSaveBg:
                ld      a,(PtrX)
                ld      (PtrOldX),a
                srl     a
                srl     a
                srl     a
                ld      c,a
                ld      a,(PtrY)
                ld      (PtrOldY),a
                call    AddrAt          ; one address calculation for the lot
                ld      de,PtrSave
                ld      b,PTRH
PsbRow:
                ld      a,(hl)
                ld      (de),a
                inc     de
                inc     l
                ld      a,(hl)
                ld      (de),a
                inc     de
                dec     l
                call    ScrDown
                djnz    PsbRow
                ret

; ------------------------------------------------------------
;  PtrHide and PtrShow
;  Nothing may paint underneath the pointer, because PtrSaveBg
;  holds the pixels it is covering and a paint that runs while it
;  is drawn gets captured into that buffer and smeared back the
;  next time the pointer moves. Every routine that paints has
;  therefore been bracketing itself with PtrRestore and PtrDraw
;  by hand, which stops working the moment one of them calls
;  another: the inner PtrRestore puts back a buffer the outer one
;  has already spent.
;
;  These count instead, so the brackets nest. D2 needs it because
;  a panel's click handler paints a row and may also call an
;  action that closes the panel and paints the desktop.
; ------------------------------------------------------------
PtrHide:
                ld      hl,PtrHidden
                inc     (hl)
                ld      a,(hl)
                dec     a
                ret     nz                      ; already hidden by an outer call
                jp      PtrRestore
PtrShow:
                ld      hl,PtrHidden
                ld      a,(hl)
                or      a
                ret     z                       ; not hidden, so nothing to do
                dec     (hl)
                ret     nz                      ; an outer call still holds it
                call    PtrSaveBg
                jp      PtrDraw
PtrHidden:      defb    0

PtrRestore:
                ld      a,(PtrOldX)
                srl     a
                srl     a
                srl     a
                ld      c,a
                ld      a,(PtrOldY)
                call    AddrAt
                ld      de,PtrSave
                ld      b,PTRH
PrsRow:
                ld      a,(de)
                ld      (hl),a
                inc     de
                inc     l
                ld      a,(de)
                ld      (hl),a
                inc     de
                dec     l
                call    ScrDown
                djnz    PrsRow
                ret

PtrDraw:
                ld      a,(PtrX)
                and     7
                add     a,a
                ld      e,a
                ld      d,0
                ld      hl,PtrShiftTab
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)          ; DE = pre shifted image for this x
                ld      a,(PtrX)
                srl     a
                srl     a
                srl     a
                ld      c,a
                push    de
                ld      a,(PtrY)
                call    AddrAt
                pop     de
                ld      b,PTRH
PdRow:
                ld      a,(de)          ; data, left byte
                ld      c,a
                inc     de
                ld      a,(de)          ; mask, left byte
                cpl
                and     (hl)
                or      c
                ld      (hl),a
                inc     de
                inc     l
                ld      a,(de)          ; data, right byte
                ld      c,a
                inc     de
                ld      a,(de)          ; mask, right byte
                cpl
                and     (hl)
                or      c
                ld      (hl),a
                inc     de
                dec     l
                call    ScrDown
                djnz    PdRow
                ret

; ------------------------------------------------------------
;  Input
; ------------------------------------------------------------
; Sample the X port a few times. A fitted mouse sitting still
; reads the same value every time; a floating bus does not.
MouseInit:
                xor     a
                ld      (MouseOn),a
                ; all three ports reading $FF is the signature of nothing fitted
                ld      bc,$FBDF
                in      a,(c)
                cp      $FF
                jr      nz,MiMaybe
                ld      bc,$FFDF
                in      a,(c)
                cp      $FF
                jr      nz,MiMaybe
                ld      bc,$FADF
                in      a,(c)
                cp      $FF
                jr      nz,MiMaybe
                ret
MiMaybe:
                ; a fitted mouse sitting still reads the same value every
                ; time; a floating bus tracks the display and does not
                ld      bc,$FBDF
                in      a,(c)
                ld      e,a
                ld      d,8
MiLoop:
                ld      bc,$FBDF
                in      a,(c)
                cp      e
                ret     nz
                dec     d
                jr      nz,MiLoop
                ld      a,1
                ld      (MouseOn),a
                ld      bc,$FBDF
                in      a,(c)
                ld      (MouseLX),a
                ld      bc,$FFDF
                in      a,(c)
                ld      (MouseLY),a
                ret

; Kempston joystick on port 31, active high. An interface that is
; not fitted floats, so require the top three bits clear and the
; same value on eight consecutive reads.
JoyInit:
                xor     a
                ld      (JoyOn),a
                xor     a
                in      a,($1F)
                ld      e,a
                and     $E0
                ret     nz
                ld      d,8
JiLoop:
                xor     a
                in      a,($1F)
                cp      e
                ret     nz
                dec     d
                jr      nz,JiLoop
                ld      a,1
                ld      (JoyOn),a
                ret

; Merges the stick into the direction bits already in C, and pulls
; the left button down if fire is held.
ReadJoy:
                ld      a,(JoyOn)
                or      a
                ret     z
                xor     a
                in      a,($1F)
                ld      d,a
                xor     a
                in      a,($1F)
                cp      d
                ret     nz              ; the two reads disagreed, so noise
                and     $E0
                ret     nz              ; top bits set, not a Kempston
                ld      a,d
                rrca                    ; bit 0 right
                jr      nc,RjB1
                set     1,c
RjB1:           rrca                    ; bit 1 left
                jr      nc,RjB2
                set     0,c
RjB2:           rrca                    ; bit 2 down
                jr      nc,RjB3
                set     3,c
RjB3:           rrca                    ; bit 3 up
                jr      nc,RjB4
                set     2,c
RjB4:           rrca                    ; bit 4 fire
                ret     nc
                ld      a,(Buttons)
                res     1,a
                ld      (Buttons),a
                ret

; in: A = signed delta, B = current value, C = maximum
; out: A = new value, clamped to 0..C
ApplyDelta:
                ld      e,a
                ld      d,0
                bit     7,e
                jr      z,ApdPos
                ld      d,$FF
ApdPos:
                ld      l,b
                ld      h,0
                add     hl,de
                bit     7,h
                jr      nz,ApdMin
                ld      a,h
                or      a
                jr      nz,ApdMax
                ld      a,l
                cp      c
                ret     c
                ret     z
ApdMax:
                ld      a,c
                ret
ApdMin:
                xor     a
                ret

; ------------------------------------------------------------
;  ClampDelta
;  in/out: A = one frame's movement on one axis
;
;  This used to discard anything at or beyond 25 in either
;  direction, on the grounds that a floating port produces large
;  random swings. It does, but so does a mouse: 25 counts in a
;  20 ms frame is a slow drag, and anything faster than that was
;  being thrown away in full. The symptom is a pointer that moves
;  if you creep and sits still if you move it normally, which is
;  what a person reports as "the mouse does not work".
;
;  Measured twice. In the headless harness, a delta of 24 moved
;  the pointer 24 pixels and a delta of 25 moved it none. And in
;  Fuse, with the MOUSETEST build reading the ports raw, the
;  largest delta in one frame was 68 on X and 25 on Y. Both of
;  those were being discarded in full.
;
;  The real defence against a floating port is the pair of reads
;  in ReadInput that have to agree, which a bus following the
;  display cannot do. This is the second line, and its job is to
;  bound movement rather than to reject it, so it clamps and
;  keeps the sign. 64 is a quarter of the counter's range and
;  comfortably past the 68 that was measured, so the fastest
;  movement seen arrives at almost full speed, and a spurious
;  reading can still only move the pointer a quarter of a screen.
;
;  It cannot usefully be larger than 127 whatever happens: the
;  counter is eight bits, so +130 and -126 are the same byte and
;  the direction is no longer knowable.
; ------------------------------------------------------------
ClampDelta:
                or      a
                ret     z
                bit     7,a
                jr      nz,CdNeg
                cp      MOUSEMAX+1
                ret     c
                ld      a,MOUSEMAX
                ret
CdNeg:
                cp      256-MOUSEMAX
                ret     nc
                ld      a,256-MOUSEMAX
                ret

ReadInput:
                ld      a,$FF           ; nothing held until proven otherwise
                ld      (Buttons),a
                ld      a,(MouseOn)
                or      a
                jr      z,RiKeys

                ; Each axis is read twice. A real interface latches its
                ; counter so the two reads agree; a floating bus is
                ; following the display and will not, so the frame is
                ; discarded rather than acted on.
                ld      bc,$FBDF
                in      a,(c)
                ld      e,a
                in      a,(c)
                cp      e
                jr      nz,RiKeys
                ld      (NewMX),a

                ld      bc,$FFDF
                in      a,(c)
                ld      e,a
                in      a,(c)
                cp      e
                jr      nz,RiKeys
                ld      (NewMY),a

                ld      bc,$FADF
                in      a,(c)
                ld      (Buttons),a

                ld      a,(NewMX)
                ld      e,a
                ld      hl,MouseLX
                sub     (hl)
                ld      (hl),e
                call    ClampDelta
                ld      hl,PtrX
                ld      b,(hl)
                ld      c,PTRXMAX
                call    ApplyDelta
                ld      (PtrX),a

                ld      a,(NewMY)       ; the counter rises as the mouse is
                ld      e,a             ; pushed away, so invert for screen Y
                ld      hl,MouseLY
                sub     (hl)
                ld      (hl),e
                call    ClampDelta
                ld      hl,SetInvertY   ; reports differ on which way the Y
                bit     0,(hl)          ; counter runs, so it is a setting, and
                                        ; the panel's toggle is this same byte
                jr      z,RiNoInvY
                neg
RiNoInvY:
                ld      hl,PtrY
                ld      b,(hl)
                ld      c,PTRYMAX
                call    ApplyDelta
                ld      (PtrY),a

RiKeys:
                ; ------------------------------------------------------------
                ;  Pointer keys need a shift now that the keyboard types
                ;  The cursor keys and QAOP moved the pointer when the
                ;  keyboard was a pointer device and nothing else. With a
                ;  text field on screen they are letters and digits, so
                ;  typing O moved the pointer left and typing 5 moved it
                ;  too. The two uses cannot share an unmodified key.
                ;
                ;  EXTEND MODE, both shifts held together, is where a
                ;  Spectrum puts a third meaning for a key, so that is
                ;  where the pointer goes. It collides with nothing:
                ;  KbdDecode tests SYMBOL SHIFT first, so both shifts
                ;  already decode as SYMBOL SHIFT and no text is lost.
                ;
                ;  Which of those it is, is SetKeyPtr rather than a rule:
                ;  0 off entirely, for a machine with a mouse; 1 behind
                ;  EXTEND MODE; 2 on the bare keys, which is right while
                ;  nothing has a text cursor and wrong the moment the
                ;  notepad does.
                ;
                ;  ReadJoy is deliberately outside the gate. A joystick
                ;  is not a key and has nothing to conflict with.
                ; ------------------------------------------------------------
                ld      c,0             ; C collects direction bits
                ld      a,(SetKeyPtr)
                or      a
                jp      z,RiK8          ; keyboard pointer switched off
                dec     a
                jr      nz,RiKUngated   ; 2, the bare keys
                ld      a,$FE           ; CAPS SHIFT Z X C V
                in      a,($FE)
                bit     0,a
                jp      nz,RiK8         ; CAPS is up, so no pointer keys
                ld      a,$7F           ; SPACE SYM M N B
                in      a,($FE)
                bit     1,a
                jp      nz,RiK8         ; nor is SYMBOL SHIFT
RiKUngated:
                ld      a,$F7           ; 1 2 3 4 5
                in      a,($FE)
                bit     4,a             ; 5 is left
                jr      nz,RiK1
                set     0,c
RiK1:
                ld      a,$EF           ; 0 9 8 7 6
                in      a,($FE)
                ld      e,a
                bit     4,e             ; 6 is down
                jr      nz,RiK2
                set     3,c
RiK2:           bit     3,e             ; 7 is up
                jr      nz,RiK3
                set     2,c
RiK3:           bit     2,e             ; 8 is right
                jr      nz,RiK4
                set     1,c
RiK4:
                ld      a,$DF           ; P O I U Y
                in      a,($FE)
                ld      e,a
                bit     0,e             ; P is right
                jr      nz,RiK5
                set     1,c
RiK5:           bit     1,e             ; O is left
                jr      nz,RiK6
                set     0,c
RiK6:
                ld      a,$FB           ; Q W E R T
                in      a,($FE)
                bit     0,a             ; Q is up
                jr      nz,RiK7
                set     2,c
RiK7:
                ld      a,$FD           ; A S D F G
                in      a,($FE)
                bit     0,a             ; A is down
                jr      nz,RiK8
                set     3,c
RiK8:
                call    ReadJoy         ; stick merges into the same bits
                ld      a,c
                ld      (Dirs),a

                ; A digital device needs a ramp or the pointer is either
                ; twitchy or glacial. One pixel to start for placing it
                ; accurately, rising to seven after about two thirds of
                ; a second of holding.
                or      a
                jr      nz,RiHeld
                xor     a
                ld      (HoldCnt),a
                jr      RiSpeed
RiHeld:
                ld      a,(HoldCnt)
                cp      32
                jr      nc,RiSpeed
                inc     a
                ld      (HoldCnt),a
RiSpeed:
                ld      a,(HoldCnt)
                srl     a
                srl     a
                srl     a
                ld      e,a
                ld      d,0
                ld      hl,(AccelPtr)
                add     hl,de
                ld      a,(hl)
                ld      (Speed),a

                ld      b,0
                ld      a,(Dirs)
                bit     0,a
                jr      z,RiNoLeft
                ld      a,(Speed)
                neg
                ld      b,a
RiNoLeft:
                ld      a,(Dirs)
                bit     1,a
                jr      z,RiNoRight
                ld      a,(Speed)
                ld      b,a
RiNoRight:
                ld      a,b
                or      a
                jr      z,RiVert
                ld      hl,PtrX
                ld      b,(hl)
                ld      c,PTRXMAX
                call    ApplyDelta
                ld      (PtrX),a
RiVert:
                ld      b,0
                ld      a,(Dirs)
                bit     2,a
                jr      z,RiNoUp
                ld      a,(Speed)
                neg
                ld      b,a
RiNoUp:
                ld      a,(Dirs)
                bit     3,a
                jr      z,RiNoDown
                ld      a,(Speed)
                ld      b,a
RiNoDown:
                ld      a,b
                or      a
                jr      z,RiBtn
                ld      hl,PtrY
                ld      b,(hl)
                ld      c,PTRYMAX
                call    ApplyDelta
                ld      (PtrY),a
RiBtn:
                ; SPACE acts as the left button
                ld      a,$7F
                in      a,($FE)
                bit     0,a
                ret     nz
                ld      a,(Buttons)
                res     1,a             ; left button, active low
                ld      (Buttons),a
                ret


; ============================================================
;  DEVICE LAYER
;  Everything above this line talks to hardware. Everything
;  below it does not. On the Next, FillRect becomes a DMA
;  transfer and DrawPointer becomes two sprite register
;  writes, and nothing above the layer changes.
; ============================================================

; ------------------------------------------------------------
;  DevFillRect
;  in: FrX byte column, FrY pixel row, FrW width in bytes,
;      FrH rows, FrPat 16 bit pattern
;  Fills using the stack, entering an unrolled PUSH chain at
;  the right offset so no inner loop counter is paid for.
;  Requires FrX + FrW <= 32.
;
;  A2. This used to hold DI for its whole run, which does not
;  delay the interrupt, it destroys it: the Spectrum asserts INT
;  for 32 T and then lets go, so a fill covering those 32 T means
;  the frame never happened. Swept across an 8 by 24 fill in the
;  headless harness, an injected interrupt was lost at 100 of 100
;  points. That is what this rewrite fixes.
;
;  It now runs with interrupts enabled throughout. What DI was
;  protecting is SP, which walks through screen memory here and is
;  not a stack, so an interrupt would push two bytes of return
;  address into the display. The fix is not to disable the
;  interrupt but to make sure those two bytes always land
;  somewhere they are about to be overwritten anyway.
;
;  SP only ever takes two kinds of value, and each is safe for its
;  own reason.
;
;    Inside the rectangle, from the LD SP,HL that starts a row to
;    the last push that ends it. A push here writes exactly the
;    two bytes the chain's next push is about to write, so the
;    damage lasts a few T states and is then painted over.
;
;    At the low point, after the last push of a row and across the
;    DJNZ and the next row's setup. That is the one place the
;    chain will not come back to, so the chain is made one push
;    shorter than the row and those leftmost two bytes are owed.
;    They are paid one iteration later, once SP has moved into the
;    next row and cannot reach them.
;
;  So the last thing each row does is leave a two byte hole, and
;  the hole is exactly where an interrupt would land. The debt is
;  settled after the loop for the final row.
;
;  The handler is EI then RET and touches nothing but its own
;  return address, so two bytes is the whole exposure and it
;  cannot nest.
; ------------------------------------------------------------
; An odd width. The chain writes pairs and missed the third column,
; so paint the first column through HL and give the chain the rest.
FrOdd:
                ld      a,(FrX)
                ld      c,a
                ld      a,(FrY)
                call    AddrAt
                ld      a,(FrH)
                ld      b,a
                ld      a,(FrPat)
                ld      c,a
FrOddCol:
                ld      (hl),c
                call    ScrDown
                djnz    FrOddCol
                ld      hl,FrW
                dec     (hl)
                jr      z,FrOddDone             ; one column wide
                ld      hl,FrX
                inc     (hl)
                call    FrSwapPat               ; keeps the pattern's phase
                call    DevFillRect
                call    FrSwapPat
                ld      hl,FrX
                dec     (hl)
FrOddDone:
                ld      hl,FrW
                inc     (hl)
                ret

FrSwapPat:
                ld      hl,(FrPat)
                ld      a,h
                ld      h,l
                ld      l,a
                ld      (FrPat),hl
                ret

DevFillRect:
                ld      a,(FrH)
                or      a
                ret     z
                ld      a,(FrW)
                srl     a
                jr      c,FrOdd
                ret     z
                ld      (FrWHalf),a
                dec     a               ; one push shorter than the row
                ld      hl,PushChainEnd ; enter the chain from the far end
                ld      e,a
                ld      d,0
                or      a
                sbc     hl,de
                push    hl              ; chain entry, into HL after the swap
                ld      a,(FrPat)       ; the owed pair goes in as immediates,
                ld      (FrPatL),a      ; so paying the debt is two stores and
                ld      (FrPatL2),a     ; not two loads and two stores
                ld      a,(FrPat+1)
                ld      (FrPatH),a
                ld      (FrPatH2),a
                ld      hl,FrIrqPad     ; row 0 owes nothing, so its payment
                ld      (FrOwed),hl     ; is made into the pad and discarded
                ld      a,(FrX)
                ld      c,a
                ld      a,(FrY)
                call    AddrAt          ; HL = start of row 0
                ld      a,(FrW)
                ld      (FrWord),a
                ld      de,(FrWord)     ; DE = width, as a 16 bit value
                exx                     ; stash the row pointer and width
                pop     hl              ; HL = chain entry
                ld      de,(FrPat)
                ld      a,(FrH)
                ld      b,a
                ld      (FrSaveSP),sp
FrRow:
                exx
                ld      c,l             ; keep the row start
                ld      b,h
                add     hl,de           ; 16 bit, so a window at the right
                ld      sp,hl           ; edge does not wrap inside L. SP is
                                        ; inside the rectangle from here, so
                                        ; the payment below is safe to make
                ld      hl,(FrOwed)     ; pay the previous row's leftmost pair
                ld      (hl),0
FrPatL          equ     $-1
                inc     hl
                ld      (hl),0
FrPatH          equ     $-1
                ld      h,b
                ld      l,c
                ld      (FrOwed),hl     ; this row owes them next time round.
                                        ; Through HL rather than BC: LD (nn),HL
                                        ; is 16 T and the ED prefixed LD (nn),BC
                                        ; is 20, and HL has to be reloaded here
                                        ; for the stepping anyway
                inc     h               ; step to the next row
                ld      a,h
                and     7
                jr      nz,FrStepped
                ld      a,l
                add     a,SCRCOLS
                ld      l,a
                jr      c,FrStepped
                ld      a,h
                sub     8
                ld      h,a
FrStepped:
                exx
                jp      (hl)
PushChain:
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
PushChainEnd:
                djnz    FrRow
                ld      sp,(FrSaveSP)   ; must be restored before the RET,
                                        ; or it pops out of screen memory, and
                                        ; before the last payment, so a push at
                                        ; the low point cannot land on it after
                ld      hl,(FrOwed)     ; the last row is still owed its pair
                ld      (hl),0
FrPatL2         equ     $-1
                inc     hl
                ld      (hl),0
FrPatH2         equ     $-1
                ret

; ------------------------------------------------------------
;  DevFillDesk
;  Same rectangle, but painted with the desktop lattice rather
;  than a flat pattern, so erasing behind a window restores the
;  background exactly.
;
;  A2, as in DevFillRect and for the same reasons. One extra
;  wrinkle: the pattern changes from row to row, so the owed pair
;  has to be paid in the pattern of the row that owes it, not the
;  row that is paying. FdOwedPat carries it across.
;
;  The pattern is now worked out after the row bookkeeping rather
;  than before it. That is not cosmetic: SP is inside the
;  rectangle by then, which is one of the two places an interrupt
;  is harmless, and it puts every instruction of the bookkeeping
;  inside the stretch where SP is parked in the pad.
; ------------------------------------------------------------
; An odd width, as FrOdd. The first column takes the lattice a row
; at a time and the chain takes the rest.
FdOdd:
                ld      a,(FrX)
                ld      c,a
                ld      a,(FrY)
                call    AddrAt
                ld      a,(FrH)
                ld      b,a
                ld      a,(FrY)
                ld      c,a
FdOddCol:
                ld      a,c
                rrca
                jr      c,FdOddBlank
                rrca
                ld      a,(LatA1)
                jr      nc,FdOddSet
                ld      a,(LatB1)
                jr      FdOddSet
FdOddBlank:
                xor     a
FdOddSet:
                ld      (hl),a
                call    ScrDown
                inc     c
                djnz    FdOddCol
                ld      hl,FrW
                dec     (hl)
                jr      z,FdOddDone
                ld      hl,FrX
                inc     (hl)
                call    DevFillDesk
                ld      hl,FrX
                dec     (hl)
FdOddDone:
                ld      hl,FrW
                inc     (hl)
                ret

DevFillDesk:
                ld      a,(FrH)
                or      a
                ret     z
                ld      a,(FrW)
                srl     a
                jr      c,FdOdd
                ret     z
                dec     a               ; one push shorter than the row
                ld      hl,FdPushChainEnd
                ld      e,a
                ld      d,0
                or      a
                sbc     hl,de
                push    hl
                ld      hl,FrIrqPad     ; row 0 owes nothing, and pays
                ld      (FdOwed),hl     ; nothing into the pad to prove it
                xor     a
                ex      af,af'
                ld      a,(FrX)
                ld      c,a
                ld      a,(FrY)
                call    AddrAt
                ld      a,(FrW)
                ld      (FrWord),a
                ld      de,(FrWord)
                exx
                pop     hl              ; chain entry
                ld      a,(FrY)
                ld      (DeskY),a
                ld      a,(FrH)
                ld      b,a
                ld      (FrSaveSP),sp
FdRow:
                exx
                ld      c,l
                ld      b,h
                add     hl,de
                ld      sp,hl           ; SP inside the rectangle from here
                ld      hl,(FdOwed)     ; pay the previous row, in the pattern
                ex      af,af'          ; that row was painted in. A' carries
                ld      (hl),a          ; it across, at 8 T for the two swaps
                inc     hl              ; against 26 T through memory. A is
                ld      (hl),a          ; scratch for the stepping just below,
                ld      h,b             ; so nothing is lost by not swapping back
                ld      l,c
                ld      (FdOwed),hl
                inc     h
                ld      a,h
                and     7
                jr      nz,FdStepped
                ld      a,l
                add     a,SCRCOLS
                ld      l,a
                jr      c,FdStepped
                ld      a,h
                sub     8
                ld      h,a
FdStepped:
                exx
                ; the lattice pattern from the row number, worked out with
                ; shifts rather than a table lookup, because HL is the chain
                ; entry and cannot be spared for an index
                ld      a,(DeskY)
                rrca
                jr      c,FdBlank
                rrca
                jr      c,FdDots2
                ld      a,$88
LatA1           equ     $-1             ; SetApply patches the operand
                jr      FdSet
FdDots2:        ld      a,$22
LatB1           equ     $-1
                jr      FdSet
FdBlank:        xor     a
FdSet:
                ld      d,a
                ld      e,a
                ex      af,af'          ; the next row pays this row's debt in
                                        ; this row's pattern, so stash it. The
                                        ; A that comes back is scratch and is
                                        ; overwritten on the next line
                ld      a,(DeskY)
                inc     a
                ld      (DeskY),a
                jp      (hl)
FdPushChain:
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
                push    de
FdPushChainEnd:
                djnz    FdRow
                ld      sp,(FrSaveSP)   ; a real stack before the last payment
                ld      hl,(FdOwed)
                ex      af,af'
                ld      (hl),a
                inc     hl
                ld      (hl),a
                ret

DeskPats:       defb    $88,$00,$22,$00

; ------------------------------------------------------------
;  Text at an arbitrary pixel row, normal and knocked out
; ------------------------------------------------------------
; in: A = char, PxRow = pixel row, C = char column
PrintCharPx:
                ld      l,a
                ld      h,0
                add     hl,hl
                add     hl,hl
                add     hl,hl
                ld      de,CHARSET
                add     hl,de
                push    hl
                ld      a,(PxRow)
                call    AddrAt
                pop     de
                ld      b,8
PcpRow:
                ld      a,(de)
                ld      c,a
                ld      a,(PrintInv)
                or      a
                jr      z,PcpNorm
                ld      a,c
                cpl
                ld      c,a
PcpNorm:
                ld      a,c
                ld      (hl),a
                inc     de
                call    ScrDown
                djnz    PcpRow
                ret

; ------------------------------------------------------------
;  PrintStrPx2
;  Faster replacement for PrintStrPx. Measured costs of the old
;  one, per character: 232 T testing PrintInv once per pixel
;  row, 344 T on eight CALLs to ScrDown, and 126 T recomputing
;  the screen address for a character whose neighbour is one
;  byte to the right. All three are avoidable.
;
;  So: the inversion becomes a self modified XOR operand set
;  once for the whole string, the screen address is computed
;  once and walked across the string a byte at a time, and the
;  eight rows are split into two straight runs either side of
;  the one cell boundary they can cross, so the boundary is
;  tested once instead of eight times.
;
;  in: HL = string, PxRow = pixel row, C = char column.
;  The string must not run past byte column 31.
; ------------------------------------------------------------
PrintStrPx2:
                ld      a,(PrintInv)
                or      a
                ld      a,0
                jr      z,Ps2NoInv
                ld      a,$FF
Ps2NoInv:
                ld      (Ps2MaskA+1),a
                ld      (Ps2MaskB+1),a
                push    hl
                ld      a,(PxRow)
                call    AddrAt
                ld      (Ps2Scr),hl
                ld      a,(PxRow)
                and     7
                neg
                add     a,8             ; rows before the cell boundary, 1 to 8
                ld      (Ps2Run1),a
                pop     hl
Ps2Loop:
                ld      a,(hl)
                or      a
                ret     z
                push    hl
                ld      l,a
                ld      h,0
                add     hl,hl
                add     hl,hl
                add     hl,hl
                ld      de,CHARSET
                add     hl,de
                ex      de,hl           ; DE = glyph
                ld      hl,(Ps2Scr)     ; HL = screen
                ld      a,(Ps2Run1)
                ld      b,a
Ps2RunA:
                ld      a,(de)
Ps2MaskA:
                xor     0               ; operand written at entry
                ld      (hl),a
                inc     de
                inc     h
                djnz    Ps2RunA
                ; the one cell boundary this character can cross
                ld      a,l
                add     a,SCRCOLS
                ld      l,a
                jr      c,Ps2Crossed
                ld      a,h
                sub     8
                ld      h,a
Ps2Crossed:
                ld      a,(Ps2Run1)
                neg
                add     a,8             ; rows after the boundary, 0 to 7
                jr      z,Ps2Next
                ld      b,a
Ps2RunB:
                ld      a,(de)
Ps2MaskB:
                xor     0
                ld      (hl),a
                inc     de
                inc     h
                djnz    Ps2RunB
Ps2Next:
                ld      hl,Ps2Scr
                inc     (hl)            ; one byte column to the right
                pop     hl
                inc     hl
                jr      Ps2Loop

Ps2Scr:         defw    0
Ps2Run1:        defb    0

; in: HL = string, PxRow = pixel row, C = char column
PrintStrPx:
                ld      a,(hl)
                or      a
                ret     z
                push    hl
                push    bc
                call    PrintCharPx
                pop     bc
                pop     hl
                inc     hl
                inc     c
                jr      PrintStrPx

; ============================================================
;  WINDOW MANAGER
; ============================================================

; The title bar is solid black with the text knocked out of it.
; That is cheaper than a striped bar and it is not a borrowed
; look: in a one bit design, inversion is the only emphasis the
; machine actually has.

WinCapH         equ     10              ; top border plus title bar

WinDraw:
                call    WdFills
                call    WdEdges
                call    WdCloseBox
                call    WdText
                ret

; The four phases are separate entry points so each can be timed on
; its own. WinDraw itself is unchanged in what it paints, which the
; bench checks with a screen checksum rather than by eye.
WdFills:
                ; black cap: top border and title bar together
                ld      a,(WinX)
                ld      (FrX),a
                ld      a,(WinY)
                ld      (FrY),a
                ld      a,(WinW)
                ld      (FrW),a
                ld      a,WinCapH
                ld      (FrH),a
                ld      hl,$FFFF
                ld      (FrPat),hl
                call    DevFillRect

                ; B4. The window that is not in front has its cap knocked
                ; back out again, leaving a one pixel frame. Nothing is
                ; added: the inversion the system already spends on chrome
                ; is withheld, and the eye reads the filled one as nearer,
                ; which is what it is. A drop shadow was decided against on
                ; day one and a dotted bar at eight pixels tall reads as a
                ; rendering fault rather than a state.
                call    WndIsFront
                jr      z,WdfCapDone
                ld      a,(WinY)
                inc     a
                ld      (FrY),a
                ld      a,WinCapH-2
                ld      (FrH),a
                ld      hl,$0000
                ld      (FrPat),hl
                call    DevFillRect
                ld      a,(WinY)
                inc     a
                ld      (PxRow),a
                ld      b,WinCapH-2     ; after the fill, not before it:
                                        ; DevFillRect uses B as its row
                                        ; counter and returns it as zero,
                                        ; which made this loop run 256 times
                                        ; and write AddrAt of rows past 191
WdfCapEdge:
                push    bc
                ld      a,(WinX)
                ld      c,a
                ld      a,(PxRow)
                call    AddrAt
                ld      a,(hl)
                or      $80
                ld      (hl),a
                ld      a,(WinW)
                dec     a
                ld      e,a
                ld      d,0
                add     hl,de
                ld      a,(hl)
                or      $01
                ld      (hl),a
                ld      a,(PxRow)
                inc     a
                ld      (PxRow),a
                pop     bc
                djnz    WdfCapEdge
                ld      a,(WinX)
                ld      (FrX),a
                ld      a,(WinW)
                ld      (FrW),a
WdfCapDone:

                ; interior
                ld      a,(WinX)
                ld      (FrX),a
                ld      a,(WinY)
                add     a,WinCapH
                ld      (FrY),a
                ld      a,(WinW)
                ld      (FrW),a
                ld      a,(WinH)
                sub     WinCapH
                ld      (FrH),a
                ld      hl,$0000
                ld      (FrPat),hl
                call    DevFillRect

                ; bottom border
                ld      a,(WinY)
                ld      c,a
                ld      a,(WinH)
                add     a,c
                dec     a
                ld      (FrY),a
                ld      a,1
                ld      (FrH),a
                ld      hl,$FFFF
                ld      (FrPat),hl
                call    DevFillRect

                ret

                ; left and right edges down the interior
WdEdges:
                ld      a,(WinY)
                add     a,WinCapH
                ld      (PxRow),a
                ld      a,(WinH)
                sub     WinCapH+1
                ld      b,a
WdEdge:
                push    bc
                ld      a,(WinX)
                ld      c,a
                ld      a,(PxRow)
                call    AddrAt
                ld      a,(hl)
                or      $80
                ld      (hl),a
                ld      a,(WinW)
                dec     a
                ld      e,a
                ld      d,0
                add     hl,de
                ld      a,(hl)
                or      $01
                ld      (hl),a
                ld      a,(PxRow)
                inc     a
                ld      (PxRow),a
                pop     bc
                djnz    WdEdge

                ret

                ; close box, knocked out of the black bar
WdCloseBox:
                ld      a,(WinX)
                ld      c,a
                ld      a,(WinY)
                inc     a
                call    AddrAt
                ld      de,CloseBox
                ld      b,8
WdClose:
                ld      a,(de)
                ld      (hl),a
                inc     de
                call    ScrDown
                djnz    WdClose

                ret

                ; title and body text, knocked out of the bar
WdText:
                call    WndIsFront
                ld      a,1
                jr      z,WdtInv
                xor     a               ; ink on white, in the hollow cap
WdtInv:
                ld      (PrintInv),a
                ld      a,(WinY)
                inc     a
                ld      (PxRow),a
                ld      a,(WinX)
                add     a,2
                ld      c,a
                ld      hl,(WinTitle)
                call    PrintStrPx2
                xor     a
                ld      (PrintInv),a

                ; and then whatever owns this window puts inside it, and
                ; the frame's scroll bar over the top of that, because the
                ; bar is chrome and chrome goes last. Whether there is one
                ; is settled first, because the text has to know how wide
                ; the interior really is before it is drawn.
                call    WndBarCheck
                ld      c,APP_DRAW
                call    AppCall
                call    WndScrollBar
                jp      WndGrip

; The about window. Static text, no cursor, and nothing that can be
; typed into it, which is the point: it is the second window, and the
; questions a compositor has to answer are about two windows rather
; than about two applications. Two applications is F2.
WdAbout:
                ld      a,(WinY)
                add     a,WinCapH+3
                ld      (PxRow),a
                ld      hl,AboutLines
WdaLoop:
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                inc     hl
                ld      a,d
                or      e
                ret     z
                push    hl
                ld      a,(WinX)
                add     a,2
                ld      c,a
                ex      de,hl
                call    PrintStrPx2
                ld      a,(PxRow)
                add     a,9
                ld      (PxRow),a
                pop     hl
                jr      WdaLoop

AboutLines:     defw    TxtAb1, TxtAb2, TxtAb3, TxtAb4, 0
TxtAb1:         defb    "ZX DESK",0
TxtAb2:         defb    "48K, Z80",0
TxtAb3:         defb    "MONOCHROME",0
TxtAb4:         defb    "PIXEL EXACT",0

; paint the desktop back over wherever the window used to be
                include "script.inc"

                include "damage.inc"

WinErase:
                ld      a,(WinOldX)
                ld      (FrX),a
                ld      a,(WinOldY)
                ld      (FrY),a
                ld      a,(WinW)
                ld      (FrW),a
                ld      a,(WinH)
                ld      (FrH),a
                jp      WeFillStrip

; ------------------------------------------------------------
;  B4 stage four: a strip, erased and then composited
;  The damage rectangles were derived when the only thing that
;  could be underneath a window was the desktop. With a second
;  window the strip a drag vacates may expose part of it, so
;  filling with the lattice is now the first half of the job and
;  putting back whatever was showing through is the second.
;
;  The window being dragged is skipped, because WinRedraw blits
;  it whole immediately afterwards. It is also always the front
;  one, because a press raises before it grabs, so there is never
;  a window that has to go on top of the one being moved.
; ------------------------------------------------------------
WeFillStrip:
                ld      a,(FrX)
                ld      (ClX),a
                ld      a,(FrY)
                ld      (ClY),a
                ld      a,(FrW)
                ld      (ClW),a
                ld      a,(FrH)
                ld      (ClH),a
                ld      a,(FrW)
                cp      2
                jr      nz,WfsWide
                call    DeskFillCol
                jr      DskUnder
WfsWide:
                call    DevFillDesk
DskUnder:
                ; The strip is bare lattice now, and an icon that was
                ; showing through it is the same problem as a window one
                ; layer down. It goes back first, because a window goes
                ; over it.
                call    DskPaintIn
                ; fall through

; The strip in Cl* is now bare desktop. Anything else that was
; showing through it goes back, back to front.
WndPaintUnder:
                ld      a,(WndCount)
                cp      2
                ret     c                       ; one window, nothing under it
                call    WndFlush
                ld      a,(WndCur)
                ld      (WpuKeep),a
                ld      a,(WndCount)
                ld      b,a
WpuLoop:
                push    bc
                ld      a,b
                dec     a                       ; back to front
                ld      e,a
                ld      d,0
                ld      hl,WndZ
                add     hl,de
                ld      a,(hl)
                ld      hl,WpuKeep
                cp      (hl)
                jr      z,WpuNext               ; the one being dragged
                call    WndSelect
                call    BlitClip
WpuNext:
                pop     bc
                djnz    WpuLoop
                ld      a,(WpuKeep)
                jp      WndSelect

WpuKeep:        defb    0

; ------------------------------------------------------------
;  WinPrintClip
;  in: C = byte column, HL = a string, PrintInv already set
;  Prints it truncated at the live window's right border.
;
;  Applications drew whatever their content was and assumed the
;  window was wide enough for it, which was true while every
;  window opened at the size its descriptor asked for. Tiling
;  made a sixteen column calendar out of a twenty three column
;  one and its grid ran straight through the window beside it.
;  Clipping the text is the general answer; refusing to make a
;  window narrower than its application likes would have made
;  tiling four windows impossible on a screen thirty two columns
;  wide.
; ------------------------------------------------------------
WinPrintClip:
                ld      a,(WinX)
                ld      b,a
                ld      a,(WinW)
                add     a,b
                dec     a                       ; the right border's column
                ld      b,a
                ld      a,(WinBarOn)            ; and a bar takes one more
                neg
                add     a,b
                sub     c
                ret     c                       ; starts on or past it, so
                ret     z                       ; there is nothing to show
                ld      b,a
                ld      de,ClipBuf
                push    de
WpcCopy:
                ld      a,(hl)
                or      a
                jr      z,WpcEnd
                ld      (de),a
                inc     hl
                inc     de
                djnz    WpcCopy
WpcEnd:
                xor     a
                ld      (de),a
                pop     hl
                jp      PrintStrPx2

ClipBuf:        defs    34

; ------------------------------------------------------------
;  Off screen window buffer
;  Composing a window costs 71,274 T, and the bench showed half of
;  that is three short strings and another quarter is the edge
;  loop. None of it changes while a window is being dragged, so it
;  is composed once into a buffer and copied back each frame.
;  The buffer is linear, WinW bytes to a row, so the source side of
;  the copy costs nothing to address and only the screen side pays
;  for the third and interleave layout.
;  Since F1 it comes from the heap and is WinW by WinH rather than
;  the 24 by 96 every window used to be given. WinBufP is where it
;  landed.
;
;  There is still a widest window, and F1 was wrong to say there
;  was not. RectBlit copies a row through an unrolled chain of
;  LDIs entered at LdiChainEnd minus twice the width, so the chain
;  is the limit and it was twenty four long, which is where the
;  old WINBUFW came from. Deleting that constant as dead did not
;  delete the limit, it only stopped anything stating it, and a
;  thirty column window then entered the chain sixty bytes before
;  it started and ran whatever was in front of it.
;
;  The chain is thirty two long now, which is the screen, so the
;  limit is a property of the display rather than of this routine.
; ------------------------------------------------------------
WINMAXW         equ     SCRCOLS         ; the LDI chain, and the screen
;  Where the buffers live
;  They used to sit between the code and the stack, which meant
;  the code and the buffers were competing for the same 10,240
;  bytes. Adding the notepad left the bench harness with 23 bytes
;  of headroom, so that arrangement had run out.
;
;  $C000 upwards is the largest free block on the machine and
;  nothing was using it. Moving the buffers there gives the code
;  everything up to the stack, 15,616 bytes, and gives the
;  buffers 16,384 to grow into.
;
;  What this costs. On a 128K, $C000 is a paged bank rather than
;  fixed RAM. DetectMachine ends by selecting bank 0 and nothing
;  in the system pages again, so the buffers are stable on both
;  machines today. F3, the 128K banking backend, is the package
;  that changes that, and when it does these buffers must either
;  be pinned to bank 0 or move. This is written down here rather
;  than discovered later.
;
;  DetectMachine writes its markers to $C000, which since F1 is
;  the storage layer's first file chunk rather than a window
;  buffer. That is harmless: it happens before StInit, which marks
;  every directory entry unused.
; ------------------------------------------------------------
BUFBASE         equ     $C000

; ------------------------------------------------------------
;  Where the fixed buffers are
;  These were each declared in the backend that owns them, which
;  read well until the file layer moved into the slow region and
;  the notepad, which is above it, wanted TAPEBUF to work out
;  where its own document lives. A chain of addresses that has to
;  be resolved before the code that defines them is assembled is
;  a chain that belongs in the memory map, and this is the memory
;  map.
; ------------------------------------------------------------
RAMFILES        equ     4
RAMNAMESZ       equ     11              ; ten characters and a terminator
RAMENTSZ        equ     16
RAMCHUNK        equ     256             ; and exactly one document
RamHeap         equ     BUFBASE                         ; $C000, 1024 bytes
RamDir          equ     RamHeap + RAMFILES*RAMCHUNK
TAPEBUF         equ     RamDir + RAMFILES*RAMENTSZ
TAPEMAX         equ     512
CODETOP         equ     STACKTOP        ; build.sh refuses a tape past this

; Screen rectangle into the buffer. Called after WinDraw, never
; during a drag, so its cost does not matter.
; Copy the current window rectangle into its buffer.
WinGrab:
                call    WinToRc
                jp      RectGrab

; Set the rectangle registers from the window model.
WinToRc:
                ld      a,(WinX)
                ld      (RcX),a
                ld      a,(WinY)
                ld      (RcY),a
                ld      a,(WinW)
                ld      (RcW),a
                ld      a,(WinH)
                ld      (RcH),a
                ld      hl,(WinBufP)
                ld      (RcBuf),hl
                ret

; Screen rectangle into RcBuf. Cost does not matter: this runs when
; a window's contents change, not while one is being dragged.
RectGrab:
                ld      a,(RcX)
                ld      c,a
                ld      a,(RcY)
                call    AddrAt
                ld      de,(RcBuf)
                ld      a,(RcH)
                ld      b,a
WgRow:
                push    bc
                push    hl
                ld      a,(RcW)
                ld      c,a
                ld      b,0
                ldir
                pop     hl
                call    ScrDown
                pop     bc
                djnz    WgRow
                ret

; Buffer back to the screen at the current window position.
;   HL  source, walks the buffer on its own as LDI increments it
;   DE  destination
;   BC  LDI's counter, decremented and never tested
;   IX  entry into the LDI chain, so the width costs no inner loop
;   B'  rows remaining
; A is not swapped by EXX, which is what lets the row count cross
; the register set without a spare byte of memory.
BlitRect:
                call    WinToRc
RectBlit:
                ld      a,(RcW)
                or      a
                ret     z
                cp      WINMAXW+1       ; wider than the chain would mean
                ret     nc              ; entering it before it starts, which
                                        ; is not a wrong picture, it is running
                                        ; whatever happens to be in front
                ld      (BrSubOp+1),a   ; the row step subtracts the width
                add     a,a             ; each LDI is two bytes
                ld      e,a
                ld      d,0
                ld      hl,LdiChainEnd
                or      a
                sbc     hl,de
                push    hl
                pop     ix
                ld      a,(RcH)
                or      a
                ret     z
                exx
                ld      b,a
                exx
                ld      a,(RcX)
                ld      c,a
                ld      a,(RcY)
                call    AddrAt
                ex      de,hl           ; DE screen, HL buffer
                ld      hl,(RcBuf)
                ld      bc,$FFFF
                jp      (ix)
LdiChain:
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
LdiChainEnd:
                ld      a,e
BrSubOp:
                sub     0               ; operand written at entry
                ld      e,a
                jr      c,BrCarried     ; The LDIs advanced DE sixteen bits, so
                                        ; if start plus width crossed a page D
                                        ; has already been incremented and the
                                        ; row step has effectively happened.
                                        ; Subtracting the width from E alone
                                        ; and then incrementing D regardless
                                        ; put the row one page too high, which
                                        ; only bites when a rectangle's row
                                        ; start plus its width crosses 256. The
                                        ; one window lived at column 8 and was
                                        ; eight bytes short of it for eight
                                        ; years of this project; B4 moved a
                                        ; window to column 16 and it appeared
                                        ; at once, as four rows painted below
                                        ; the window.
                inc     d
BrCarried:
                ld      a,d
                and     7
                jr      nz,BrStepped
                ld      a,e
                add     a,SCRCOLS
                ld      e,a
                jr      c,BrStepped
                ld      a,d
                sub     8
                ld      d,a
BrStepped:
                exx
                djnz    BrCont
                exx
                ret
BrCont:
                exx
                jp      (ix)

; ------------------------------------------------------------
;  B4: BlitClip
;  in: ClX, ClY, ClW, ClH = a damage rectangle, and the live
;      window record = the window to paint into it
;
;  Paints the intersection of the two, reading the matching sub
;  rectangle of the window's buffer. This is what makes a second
;  window affordable: repainting a whole window that the damage
;  merely touches is about 13,000 T for a 16 by 72 one, a drag
;  frame already costs 59,858 T of 69,888, and the two together
;  overrun.
;
;  It has its own LDI chain rather than sharing RectBlit's. The
;  chains are identical; the tails are not, because this one has
;  to step the buffer pointer on by the difference between the
;  window's width and the width being copied, and RectBlit is on
;  the drag path and should not pay for a stride it never has.
; ------------------------------------------------------------
BlitClip:
                ld      a,(WinX)                ; x0 = max(WinX, ClX)
                ld      b,a
                ld      a,(ClX)
                cp      b
                jr      nc,BcHaveX0
                ld      a,b
BcHaveX0:
                ld      (BcX),a
                ld      a,(WinX)                ; x1 = min(WinX+WinW, ClX+ClW)
                ld      hl,WinW
                add     a,(hl)
                ld      b,a
                ld      a,(ClX)
                ld      hl,ClW
                add     a,(hl)
                cp      b
                jr      c,BcHaveX1
                ld      a,b
BcHaveX1:
                ld      hl,BcX
                sub     (hl)
                ret     z                       ; no overlap in x
                ret     c
                ld      (BcW),a

                ld      a,(WinY)                ; and the same in y
                ld      b,a
                ld      a,(ClY)
                cp      b
                jr      nc,BcHaveY0
                ld      a,b
BcHaveY0:
                ld      (BcY),a
                ld      a,(WinY)
                ld      hl,WinH
                add     a,(hl)
                ld      b,a
                ld      a,(ClY)
                ld      hl,ClH
                add     a,(hl)
                cp      b
                jr      c,BcHaveY1
                ld      a,b
BcHaveY1:
                ld      hl,BcY
                sub     (hl)
                ret     z
                ret     c
                ld      (BcH),a

                ; the buffer address of the top left of the intersection
                ld      a,(BcY)
                ld      hl,WinY
                sub     (hl)
                ld      b,a                     ; rows down into the buffer
                ld      hl,(WinBufP)
                ld      a,(WinW)
                ld      e,a
                ld      d,0
                ld      a,b
                or      a
                jr      z,BcAtRow
BcDown:
                add     hl,de
                djnz    BcDown
BcAtRow:
                ld      a,(BcX)
                ld      c,a
                ld      a,(WinX)
                neg
                add     a,c                     ; columns in from the left
                ld      e,a
                ld      d,0
                add     hl,de
                ld      (BcSrc),hl

                ; how far the buffer pointer has to jump at the end of a row
                ld      a,(WinW)
                ld      hl,BcW
                sub     (hl)
                ld      (BcAdjOp+1),a

                ld      a,(BcW)
                ld      (BcSubOp+1),a           ; the screen step subtracts it
                add     a,a                     ; each LDI is two bytes
                ld      e,a
                ld      d,0
                ld      hl,BcChainEnd
                or      a
                sbc     hl,de
                push    hl
                pop     ix
                ld      a,(BcH)
                exx
                ld      b,a
                exx
                ld      a,(BcX)
                ld      c,a
                ld      a,(BcY)
                call    AddrAt
                ex      de,hl                   ; DE screen, HL buffer
                ld      hl,(BcSrc)
                ld      bc,$FFFF
                jp      (ix)
BcChain:
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
                ldi
BcChainEnd:
                ld      a,l                     ; the buffer skips the part of
BcAdjOp:
                add     a,0                     ; the row it did not copy
                ld      l,a
                jr      nc,BcSrcOk
                inc     h
BcSrcOk:
                ld      a,e                     ; the screen steps a row
BcSubOp:
                sub     0
                ld      e,a
                jr      c,BcCarried             ; as in RectBlit, and for the
                inc     d                       ; same reason
BcCarried:
                ld      a,d
                and     7
                jr      nz,BcStepped
                ld      a,e
                add     a,SCRCOLS
                ld      e,a
                jr      c,BcStepped
                ld      a,d
                sub     8
                ld      d,a
BcStepped:
                exx
                djnz    BcCont
                exx
                ret
BcCont:
                exx
                jp      (ix)

ClX:            defb    0
ClY:            defb    0
ClW:            defb    0
ClH:            defb    0
BcX:            defb    0
BcY:            defb    0
BcW:            defb    0
BcH:            defb    0
BcSrc:          defw    0

CloseBox:       defb    $FF,$81,$BD,$BD,$BD,$BD,$81,$FF

; ------------------------------------------------------------
;  Dragging
;  X snaps to byte columns. Pixel accurate horizontal movement
;  would need the whole window pre shifted, which costs far
;  more than it is worth; vertical stays pixel accurate.
; ------------------------------------------------------------
                include "saveunder.inc"

; The bench build is short of fast region, so its settings code
; goes in the slow one. None of it runs inside a frame.
IFNDEF BENCH
                include "settings.inc"
ENDIF
                include "setdata.inc"

                include "menus.inc"

                include "hittest.inc"

                include "kbd.inc"

                include "events.inc"

                include "heap.inc"

                include "app.inc"

                include "note.inc"

                include "mousetest.inc"

                include "resize.inc"

                ; The clock stays in the fast region although it is an
                ; application, because ClkTick runs every frame. It is
                ; forty T states when nothing has happened, which is
                ; nothing, and the rule for the slow region is not "cheap
                ; enough", it is "not in a frame".
                include "clock.inc"

                include "sound.inc"

                ; Back in the fast region. It is the largest single thing
                ; that had gone down, the slow region was left with 672
                ; bytes and the fast one with four thousand, and a region
                ; nothing more can go into is not headroom.
                include "panel.inc"

                ; Desktop chrome, but in the fast region: DskPaintIn runs
                ; on the drag path, because a window dragged across an icon
                ; vacates a strip that the icon was showing through.
                include "desktop.inc"

; The editor subject is TEST only. The Fuse report showed NOTE 17 of
; 17 and the headless harness checks the same seventeen things and
; then reads every glyph back off the screen, so the Fuse copy was
; the weaker of two and it was the one costing bytes in the build
; with 774 of them left.
IFDEF TEST
                include "notetest.inc"
ENDIF

; ------------------------------------------------------------
;  Window drag, as event handlers
;  This was DoDrag, called unconditionally from MainLoop every
;  frame and polling the button state itself. It is now three
;  handlers registered in EvHandlers, so MainLoop no longer knows
;  that windows exist.
; ------------------------------------------------------------
HdlBtnUp:
                xor     a
                ld      (Dragging),a
                call    RszEnd                  ; which returns at once unless
                                                ; a resize was in progress
                jp      DskRelease              ; and so does this

HdlPtrMove:
                ld      a,(Resizing)
                or      a
                jp      nz,RszMove
                ld      a,(DskDrag)
                or      a
                jp      nz,DskDragMove
                ld      a,(Dragging)
                or      a
                ret     z
                jp      DdMove

HdlBtnDown:
                ld      a,(PtrX)
                srl     a
                srl     a
                srl     a
                ld      b,a             ; pointer byte column
                ld      a,(PtrY)
                ld      c,a
                ; The settings panel is modal: while it is up it takes every
                ; press and nothing behind it moves.
                ld      a,(DlgUp)
                or      a
                jr      z,HbdNoDlg
                push    bc
                call    HitTest
                pop     bc
                cp      CTL_DIALOG
                ret     nz
                jp      DlgClick
HbdNoDlg:
                ; An open menu swallows the next press wherever it lands,
                ; which is what makes clicking away from it dismiss it.
                ld      a,(MenuOpen)
                or      a
                jr      z,HbdNoMenu
                push    bc
                call    HitTest
                pop     bc
                cp      CTL_MENUDROP
                jr      z,HbdPick
                ld      a,$FF                   ; clicked away: no selection
                ld      (MenuPick),a
                jr      HbdShut
HbdPick:
                call    MenuItemAt
                ld      (MenuPick),a
HbdShut:
                ld      a,(MenuOpen)
                ld      (MnLastMenu),a
                call    MenuClose
                ld      a,(MenuPick)
                cp      $FF
                ret     z
                jp      MenuAction
HbdNoMenu:
                push    bc
                call    HitTest
                pop     bc
                cp      CTL_MENUBAR
                jr      z,HbdBar
                ; B4. Windows are hit tested by walking the z order rather
                ; than by three rows in the control table, because those
                ; rows described the only window there was. The control
                ; table keeps the things there is one of: the menu bar, the
                ; drop down, the panel and the desktop.
                push    bc
                call    WndHitTest
                pop     bc
                cp      CTL_NONE
                jr      z,HbdDesk
                push    af
                ld      a,(WndHit)
                call    WndFocusTo      ; a press raises and focuses, because
                pop     af              ; those are the same thing
                cp      CTL_CLOSEBOX
                jp      z,WndClose
                cp      CTL_RESIZE
                jp      z,RszStart
                cp      CTL_SCROLL
                jr      nz,HbdNotScroll
                ld      a,(PtrY)
                ld      c,a
                jp      ScrPress
HbdNotScroll:
                cp      CTL_TITLEBAR
                ret     nz              ; a press in the body does nothing yet
                                        ; beyond having raised it
                jr      HbdGrab
HbdDesk:
                ; Nothing under the pointer but desktop, which is where the
                ; shortcuts live. A press only records itself: whether it
                ; was an opening or the start of a move is not known until
                ; the pointer either moves or does not.
                call    DskHit
                or      a
                ret     z
                jp      DskPress

HbdBar:
                call    MenuHitTitle
                or      a
                ret     z
                jp      MenuOpenDrop
HbdGrab:
                ; The blit reads the grabbed buffer, so any text typed since
                ; the last grab has to be folded in before the window moves.
                ; Deferring it to here rather than paying it per keystroke is
                ; what keeps a typing frame cheap.
                call    NoteSync
                ; grabbed. remember where within the bar
                ld      a,1
                ld      (Dragging),a
                ld      a,(PtrX)
                srl     a
                srl     a
                srl     a
                ld      c,a
                ld      a,(WinX)
                neg
                add     a,c
                ld      (DragDX),a
                ld      a,(WinY)
                ld      c,a
                ld      a,(PtrY)
                sub     c
                ld      (DragDY),a
DdNo:
                ret

DdMove:
                ld      a,(WinX)
                ld      (WinOldX),a
                ld      a,(WinY)
                ld      (WinOldY),a

                ; Underflow has to be read from the borrow flag. Testing
                ; bit 7 looks like a sign test but a legitimate row of 137
                ; has bit 7 set, which sent the window to the top of the
                ; screen the moment it was dragged past halfway.
                ld      a,(PtrX)
                srl     a
                srl     a
                srl     a
                ld      hl,DragDX
                sub     (hl)
                jr      nc,DdXpos
                xor     a               ; dragged off the left edge
DdXpos:
                ld      c,a
                ld      a,(WinW)
                neg
                add     a,SCRCOLS       ; rightmost column that still fits
                cp      c
                jr      nc,DdXset
                ld      c,a
DdXset:
                ld      a,c
                ld      (WinX),a

                ld      a,(PtrY)
                ld      hl,DragDY
                sub     (hl)
                jr      nc,DdYpos
                ld      a,MENUH         ; dragged off the top
DdYpos:
                cp      MENUH
                jr      nc,DdYmax
                ld      a,MENUH
DdYmax:
                ld      c,a
                ld      a,(WinH)
                neg
                add     a,STATRULE      ; lowest row that still fits
                cp      c
                jr      nc,DdYset
                ld      c,a
DdYset:
                ld      a,c
                ld      (WinY),a

                ; did it actually move?
                ld      a,(WinOldX)
                ld      c,a
                ld      a,(WinX)
                cp      c
                jr      nz,DdMoved
                ld      a,(WinOldY)
                ld      c,a
                ld      a,(WinY)
                cp      c
                ret     z
DdMoved:
                ld      a,1
                ld      (WinMoved),a
                ret

; Redraw when the window has moved. The pointer is lifted first
; so that its saved background is never stale, and put back
; afterwards.
WinRedraw:
                ld      a,(WinMoved)
                or      a
                ret     z
                ; No PtrRestore here. The redraw now starts while the beam is
                ; still inside the window, so lifting the pointer at this point
                ; and putting it back after the blit would leave it absent when
                ; the beam reaches its row. The blit paints over the pointer
                ; instead, and the PtrRestore at the top of the next frame,
                ; which runs in the border, is what clears any ghost.
                call    WinEraseDamage  ; only the strip the window has vacated
                call    BlitRect        ; not WinDraw: the window is already composed
                call    PtrSaveBg
                call    PtrDraw
                xor     a               ; the paint has caught up with the model
                ld      (WinMoved),a
                ret

; ------------------------------------------------------------
;  B4: the window list
;  WndZ is the z order, front first. It is also the paint order
;  reversed and the hit test order, so there is one list rather
;  than three.
; ------------------------------------------------------------
; A = window index. Returns HL = its record in the table.
WndRecAt:
                ld      l,a
                ld      h,0
                ld      de,WNDRECSZ
                ld      b,a
                ld      hl,WndTab
                or      a
                ret     z
WraLoop:
                add     hl,de
                djnz    WraLoop
                ret

; A = window index. Makes it the live record, writing the outgoing
; one back first. Selecting the window that is already current costs
; a compare and a return.
WndSelect:
                ld      hl,WndCur
                cp      (hl)
                ret     z
                push    af
                call    AppSave                 ; the outgoing window's
                                                ; document, before the record
                                                ; that points at it is gone
                ld      a,(WndCur)
                call    WndRecAt
                ex      de,hl
                ld      hl,WinRec
                ld      bc,WNDRECSZ
                ldir                            ; the outgoing window, saved
                pop     af
                ld      (WndCur),a
                call    WndRecAt
                ld      de,WinRec
                ld      bc,WNDRECSZ
                ldir
                jp      AppLoad                 ; and the incoming one's

; Z set if the live window is the one at the front of the z order,
; which is the same thing as the one with the keyboard. A system where
; the window holding the keyboard is not the window in front would
; need a second kind of highlight to explain itself, and this display
; has one bit.
WndIsFront:
                ld      a,(WndZ)
                ld      hl,WndCur
                cp      (hl)
                ret

; The live record is newer than its copy in the table, so anything
; that walks the table has to write it back first. Since F2 the live
; document is newer than its block for the same reason, and it is the
; same one line fix in the same one place.
WndFlush:
                call    AppSave
                ld      a,(WndCur)
                call    WndRecAt
                ex      de,hl
                ld      hl,WinRec
                ld      bc,WNDRECSZ
                ldir
                ret

; A = the window to bring to the front. Anything in front of it slides
; back one place, which is what makes this a list and not a swap: with
; three windows a swap would put the wrong one in the middle.
WndRaise:
                ld      b,a
                ld      a,(WndZ)
                cp      b
                ret     z                       ; already there
                ld      hl,WndZ
                ld      c,0                     ; how far down it was found
WrFind:
                ld      a,(hl)
                cp      b
                jr      z,WrFound
                inc     hl
                inc     c
                ld      a,c
                ld      de,WndCount
                ld      a,(de)
                cp      c
                jr      nz,WrFind
                ret                             ; not in the list at all
WrFound:
                ; HL points at it; shuffle everything above it down one
                ld      a,c
                or      a
                ret     z
WrShift:
                dec     hl
                ld      a,(hl)
                inc     hl
                ld      (hl),a
                dec     hl
                dec     c
                jr      nz,WrShift
                ld      (hl),b                  ; and it goes to the front
                ret

; Repaint the desktop and every window, back to front. Only wanted
; when the z order changes or a window opens or closes, never in a
; frame, so the cost is a user action rather than a budget item.
WndRepaintAll:
                call    WndFlush
                call    DrawDesktop
                ld      a,(WndCount)
                ld      b,a
WraPaint:
                push    bc
                ld      a,b
                dec     a                       ; WndCount-1 down to 0, which
                ld      e,a                     ; is back to front
                ld      d,0
                ld      hl,WndZ
                add     hl,de
                ld      a,(hl)
                call    WndSelect
                call    WinDraw                 ; recompose, so the focus
                call    WinGrab                 ; state of the cap is current
                pop     bc
                djnz    WraPaint
                ret

; ------------------------------------------------------------
;  Opening, closing and focusing
; ------------------------------------------------------------
; A = kind. Carry set if there is no slot free.
WndOpen:
                ; The kind goes to memory rather than a register. WndFlush
                ; ends in an LDIR, so it returns with A clobbered and BC
                ; zero, and holding an argument across it costs a bug that
                ; looks like a geometry bug: the second window came out as
                ; an exact copy of the first.
                ld      (WndWantKind),a
                ld      a,(WndCount)
                cp      WNDMAX
                jp      nc,WndNoRoom            ; JP: the geometry F2 reads
                                                ; out of the descriptor put
                                                ; this past 127 bytes
                call    WndFlush
                ; The slot was WndCount, which is only the free one while
                ; closing always removes the highest. It does not: WndClose
                ; closes the front, and the front is whatever was last
                ; pressed. Close the middle of three and the count says two
                ; while slot two is still live, so the next open wrote its
                ; record over a window that was on screen and put its index
                ; into the z order twice. It was reachable with two windows
                ; and is more so with four.
                call    WndFreeSlot
                jp      c,WndNoRoom
                ld      (WndCur),a              ; the new slot. Its contents
                                                ; are junk and are all about
                                                ; to be written
                ld      hl,0                    ; including the two pointers,
                ld      (WinBufP),hl            ; which are still the outgoing
                ld      (WinStateP),hl          ; window's and are not this
                                                ; record's to give back
                ; F2. Geometry, title and the three vectors all come out of
                ; the application's descriptor. This was an IF on the kind
                ; with the about window's numbers written into it, so a
                ; third application meant editing the window code.
                ld      a,(WndWantKind)
                call    AppAt
                ld      (WinApp),hl
                ld      de,APP_TITLE
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                inc     hl
                ex      de,hl
                ld      (WinTitle),hl
                ex      de,hl                   ; HL now at APP_X
                ; The size first, because the cascade below is clamped
                ; against it and a clamp cannot use a width that has not
                ; been read yet.
                push    hl
                inc     hl
                inc     hl
                ld      a,(hl)
                ld      (WinW),a
                inc     hl
                ld      a,(hl)
                ld      (WinH),a
                pop     hl
                ; Instances cascade. Two notepads at the same place look
                ; like one window, and a system whose whole point is that
                ; there can be two should not have to be dragged before it
                ; can be believed. Clamped, because the about window is
                ; already 29 columns wide at its own x and three steps of
                ; the cascade would put its right edge past the screen,
                ; where RectBlit wraps to the next row rather than
                ; complaining.
                ld      a,(WndCount)
                add     a,a
                add     a,(hl)
                ld      c,a
                ld      a,(WinW)
                neg
                add     a,SCRCOLS               ; the rightmost x that fits
                cp      c
                jr      nc,WoXok
                ld      c,a
WoXok:
                ld      a,c
                ld      (WinX),a
                ld      (WinOldX),a
                inc     hl
                ld      a,(WndCount)
                add     a,a
                add     a,a
                add     a,a                     ; eight rows a step, which is
                                                ; one line of text and enough
                                                ; that the title bar behind is
                                                ; readable rather than a hint
                add     a,(hl)
                ld      c,a
                ld      a,(WinH)
                neg
                add     a,STATRULE              ; and the lowest y
                cp      c
                jr      nc,WoYok
                ld      c,a
WoYok:
                ld      a,c
                ld      (WinY),a
                ld      (WinOldY),a
                xor     a
                ld      (WinMoved),a
                call    WndAllocBuf             ; sized to this window, not to
                jr      c,WoNoBuf               ; the largest there could be
                call    WndAllocState           ; and its own document, if it
                jr      c,WoNoBuf               ; is the sort of thing that
                                                ; has one
                ; the z order slides back one and the new window takes the
                ; front, which is also the focus, because they are the same
                ld      a,(WndCount)
                or      a
                jr      z,WoFirst               ; nothing to slide back
                ld      e,a
                ld      d,0
                ld      hl,WndZ
                add     hl,de
                ld      d,h
                ld      e,l
                dec     hl                      ; the last existing entry
                ld      b,a
WoShift:
                ld      a,(hl)
                ld      (de),a
                dec     hl
                dec     de
                djnz    WoShift
WoFirst:
                ld      a,(WndCur)
                ld      (WndZ),a
                ld      hl,WndCount
                inc     (hl)
                call    WndRepaintAll
                or      a
                ret
WndNoRoom:
                scf
                ret

; The table had a slot but the heap had no pixels for it. WndCur and
; WinRec are half of a window that is not in the z order, so put the
; front one back: without this the desktop paints correctly from a
; list the live record is not in, and the keyboard goes to a window
; nobody can see. WndSelect flushes the junk into the slot on its way
; out, which is harmless because nothing is using that slot.
WoNoBuf:
                ; Whatever the half opened window did take goes back. F1
                ; only had to put the front window back, because the one
                ; thing that could fail was the first allocation and it
                ; had taken nothing. F2 asks twice, so the buffer can be
                ; held by a window that never reaches the z order and
                ; never reaches WndClose either.
                ld      a,(WndCur)
                add     a,HEAPOWN_WIN
                call    HeapFreeOwner
                ld      hl,0                    ; and the pointers go before
                ld      (WinBufP),hl            ; WndSelect can write the
                ld      (WinStateP),hl          ; live document into a block
                                                ; that has just been freed
                ld      a,(WndZ)
                call    WndSelect
                scf
                ret

; Closes the front window. The last one does not close, because
; nothing here can open the notepad again and a desktop with no
; windows and no way back is worse than a close box that does
; nothing.
WndClose:
                ld      a,(WndCount)
                cp      2
                jr      nc,WcMayClose
                xor     a                       ; the last window does not
                ld      (WndForce),a            ; close, so a force that got
                ret                             ; this far is spent
WcMayClose:
                ; F2 shipped without a teardown vector and said so in
                ; writing: everything an application takes comes back
                ; through the owner byte, so nothing had to be asked. This
                ; is not about memory. It is about consent, and the notepad
                ; is the first thing here with something to lose.
                ;
                ; Carry set means "not yet" rather than "no". The notepad
                ; puts a question on screen and returns refused; the answer
                ; sets WndForce and asks again.
                ld      a,(WndForce)
                or      a
                jr      nz,WcGo
                or      a                       ; carry clear, so an
                ld      c,APP_CLOSE             ; application with no close
                call    AppCall                 ; vector reads as consenting
                ret     c
WcGo:
                xor     a
                ld      (WndForce),a
                ld      hl,WndZ
                ld      de,WndZ+1
                ld      a,(WndCount)
                dec     a
                ld      b,a
WcShift:
                ld      a,(de)
                ld      (hl),a
                inc     hl
                inc     de
                djnz    WcShift
                ; whatever that window took from the heap goes back, and
                ; the window code does not have to remember what it was
                ld      a,(WndCur)
                add     a,HEAPOWN_WIN
                call    HeapFreeOwner
                ld      hl,WndCount
                dec     (hl)
                ld      a,(WndZ)
                ld      (WndCur),a              ; its record is stale but the
                call    WndRecAt                ; table's copy is not
                ld      de,WinRec
                ld      bc,WNDRECSZ
                ldir
                call    AppLoad                 ; the window behind is live
                jp      WndRepaintAll           ; now, and so is its document

; A = a window. Brings it to the front and makes it live, repainting
; only if the order actually changed.
WndFocusTo:
                ld      hl,WndZ
                cp      (hl)
                jp      z,WndSelect             ; already in front. JP, because
                                                ; relative jumps go out of
                                                ; range easily in this codebase
                call    WndRaise
                ld      a,(WndZ)
                call    WndSelect
                jp      WndRepaintAll

; ------------------------------------------------------------
;  WndHitTest
;  in:  B = pointer byte column, C = pointer pixel row
;  out: A = which part was hit, WndHit = which window
;  Walks the z order front to back, so the first hit wins and the
;  list that is the paint order is also the hit order.
; ------------------------------------------------------------
WndHitTest:
                ld      a,b
                ld      (WhtCol),a
                ld      a,c
                ld      (WhtRow),a
                call    WndFlush
                ld      a,(WndCount)
                ld      b,a
                ld      c,0
WhtLoop:
                push    bc
                ld      e,c
                ld      d,0
                ld      hl,WndZ
                add     hl,de
                ld      a,(hl)
                ld      (WndHit),a
                call    WndRecAt
                ld      e,(hl)                  ; x
                inc     hl
                ld      d,(hl)                  ; y
                inc     hl
                ld      c,(hl)                  ; w
                inc     hl
                ld      b,(hl)                  ; h
                ld      a,(WhtCol)
                sub     e
                jr      c,WhtNext
                cp      c
                jr      nc,WhtNext
                ld      a,(WhtRow)
                sub     d
                jr      c,WhtNext
                cp      b
                jr      nc,WhtNext
                cp      WinCapH                 ; A is the row within the window
                jr      nc,WhtBody
                ld      d,a                     ; keep it: D held the window's
                                                ; y and is finished with
                ld      a,(WhtCol)
                sub     e
                jr      nz,WhtTitle
                ; B4 made the close box the whole leftmost column of the
                ; cap, and WdCloseBox draws the glyph at WinY+1 for eight
                ; rows, so the border row above it and the row below it
                ; closed the window without looking like they would. Two
                ; pixel rows, and the bench cases that named them had been
                ; reading as failures since B4 among seven others.
                ld      a,d
                or      a
                jr      z,WhtTitle
                cp      9
                jr      nc,WhtTitle
                ld      a,CTL_CLOSEBOX
                jr      WhtDone
WhtTitle:
                ld      a,CTL_TITLEBAR
                jr      WhtDone
WhtBody:
                ; The grip is the last byte column of the last eight pixel
                ; rows, which is one character cell in the bottom right.
                ; It is tested before the interior because it is inside it.
                ld      d,a                     ; the row within the window
                add     a,RSZGRIP
                cp      b
                jr      c,WhtScrollTest         ; above the grip's rows, but
                                                ; the bar runs the whole way
                                                ; down and this test is only
                                                ; about the grip
                ld      a,(WhtCol)
                sub     e                       ; the column within it
                inc     a
                cp      c
                jr      c,WhtScrollTest
                ld      a,CTL_RESIZE
                jr      WhtDone
WhtScrollTest:
                ; The bar's column. Whether this window actually has one is
                ; asked afterwards, by the press, because the descriptor
                ; that answers is the live window's and the press has
                ; raised this one by then.
                ld      a,(WhtCol)
                sub     e
                add     a,2
                cp      c
                jr      nz,WhtInterior
                ld      a,CTL_SCROLL
                jr      WhtDone
WhtInterior:
                ld      a,CTL_WININTERIOR
WhtDone:
                pop     bc
                ret
WhtNext:
                pop     bc
                inc     c
                djnz    WhtLoop
                ld      a,CTL_NONE
                ret

; The ZX DESK menu's ABOUT item. Opening it twice raises the one that
; is already there rather than failing, which is what a person doing
; it expects and costs one test.
; ------------------------------------------------------------
;  WndFindApp
;  in:  HL = an application descriptor
;  out: A = a window running it, carry set if there is none
;  Walks the z order, so the one it finds is the frontmost.
; ------------------------------------------------------------
WndFindApp:
                ld      (WfaWant),hl
                call    WndFlush
                ld      a,(WndCount)
                or      a
                jr      z,WfaNone
                ld      b,a
                ld      c,0
WfaLoop:
                push    bc
                ld      e,c
                ld      d,0
                ld      hl,WndZ
                add     hl,de
                ; The slot goes to memory, not to AF. Keeping it there
                ; and popping it after the compare restores the flags
                ; from before the compare, so the search never matched
                ; and ABOUT opened a second about window every time.
                ld      a,(hl)
                ld      (WfaSlot),a
                call    WndRecAt
                ld      de,8                    ; WinApp within the record
                add     hl,de
                ld      e,(hl)
                inc     hl
                ld      d,(hl)
                ld      hl,(WfaWant)
                or      a
                sbc     hl,de
                pop     bc
                jr      nz,WfaNext
                ld      a,(WfaSlot)
                or      a                       ; the window, and no carry
                ret
WfaNext:
                inc     c
                djnz    WfaLoop
WfaNone:
                scf
                ret

WfaWant:        defw    0
WfaSlot:        defb    0

; Opening ABOUT twice raises the one that is already there rather than
; failing, which is what a person doing it expects. It used to assume
; that window was slot one, which was true while there were two
; windows and one of them was the notepad. F2 makes a second notepad
; slot one, so ABOUT would have raised a notepad and called it done.
AboutOpen:
                ld      hl,AppAbout
                call    WndFindApp
                jp      nc,WndFocusTo
                ld      a,APP_ABOUT
                jp      WndOpen

; The clock is raised rather than duplicated, for the same reason it
; keeps no state: there is one time, and a second window showing it
; would be a second view of the same thing taking a slot.
ClockOpen:
                ld      hl,AppClock
                call    WndFindApp
                jp      nc,WndFocusTo
                ld      a,APP_CLOCK
                jp      WndOpen

; The calendar is not. Two calendars on two months is the thing a
; calendar is for, and it is also the only application whose per
; instance state can be seen from across the room.
WndOpenCal:
                ld      a,APP_CAL
                jp      WndOpen

; The commander is raised rather than duplicated. Two of them would be
; four panes over three devices, which is not a use, it is a boast.
CmdOpenWin:
                ld      hl,AppCmd
                call    WndFindApp
                jp      nc,WndFocusTo
                ld      a,APP_CMD
                jp      WndOpen

WndWantKind:    defb    0
WndForce:       defb    0               ; set by an application that has
                                        ; finished asking and wants the close
                                        ; it refused a moment ago
WhtCol:         defb    0
WhtRow:         defb    0
WndHit:         defb    0
TxtAboutTitle:  defb    "ABOUT",0
TxtClockTitle:  defb    "CLOCK",0
TxtCalTitle:    defb    "CALENDAR",0
TxtCmdTitle:    defb    "FILES",0
TxtKeysTitle:   defb    "KEYS",0
IFDEF PRINTDEMO
TxtPrintDemo:   defb    "ZX DESK PRINTS",0,0
                defb    "HELLO WORLD   ",0,0
ENDIF

; ------------------------------------------------------------
;  WndFreeSlot
;  out: A = a record slot no window is using, carry set if there
;  is none. Walks the z order rather than keeping a bitmap: with
;  four windows the walk is sixteen compares at worst and happens
;  once per open, and a bitmap is another thing to keep true.
; ------------------------------------------------------------
WndFreeSlot:
                ld      c,0                     ; the candidate
WfsTry:
                ld      a,(WndCount)
                or      a
                jr      z,WfsFound              ; nothing is using anything
                ld      b,a
                ld      hl,WndZ
WfsScan:
                ld      a,(hl)
                cp      c
                jr      z,WfsNext               ; taken, try the next one
                inc     hl
                djnz    WfsScan
WfsFound:
                ld      a,c
                or      a                       ; and clear the carry
                ret
WfsNext:
                inc     c
                ld      a,c
                cp      WNDMAX
                jr      c,WfsTry
                scf
                ret

; ------------------------------------------------------------
;  WndAllocBuf
;  Takes a buffer for the live window from the heap, W by H
;  rather than the fixed 24 by 96 every window used to get. The
;  owner is the window's own index, so closing it is one call to
;  HeapFreeOwner and the window code remembers nothing.
; ------------------------------------------------------------
WndAllocBuf:
                ld      hl,(WinBufP)            ; give back the old one first,
                call    HeapFree                ; so a resize is two calls
                ld      hl,0
                ld      (WinBufP),hl
                ld      a,(WinH)
                ld      b,a
                ld      a,(WinW)
                ld      e,a
                ld      d,0
                ld      hl,0
WabRow:
                add     hl,de
                djnz    WabRow
                ld      b,h
                ld      c,l
                ld      a,(WndCur)
                add     a,HEAPOWN_WIN
                call    HeapAlloc
                ret     c
                ld      (WinBufP),hl
                or      a
                ret

; ------------------------------------------------------------
;  WndAllocState
;  Takes this window's own copy of whatever its application
;  keeps, under the window's index, so WndClose gives it back
;  without the application being asked. An application that keeps
;  nothing costs a compare and a return, which is the about
;  window.
; ------------------------------------------------------------
WndAllocState:
                ld      hl,(WinApp)
                ld      c,(hl)
                inc     hl
                ld      b,(hl)
                ld      a,b
                or      c
                ret     z                       ; and that clears the carry
                ld      a,(WndCur)
                add     a,HEAPOWN_WIN
                call    HeapAlloc
                ret     c
                ld      (WinStateP),hl
                ld      c,APP_INIT              ; a fresh instance, so the
                call    AppCall                 ; live copy is made and then
                call    AppSave                 ; written into the new block
                or      a
                ret

; Called once. Window nought is whatever WinRec was assembled with,
; which is the notepad, so the desktop starts exactly as it did.
WndInit:
                call    HeapInit
                xor     a
                ld      (WndCur),a
                call    WndAllocBuf
                call    WndAllocState
                xor     a                       ; WndAllocBuf returns whatever
                call    WndRecAt                ; HeapAlloc left in A, and
                                                ; WndRecAt reads A as the slot
                ex      de,hl
                ld      hl,WinRec
                ld      bc,WNDRECSZ
                ldir
                xor     a
                ld      (WndZ),a
                ld      a,1
                ld      (WndCount),a
                ret

; ------------------------------------------------------------
;  Beam scheduling
;  The redraw must never write a row before the ULA has read it.
;  Rule B waits for the beam to clear the whole window, which is
;  stricter than the hardware needs: the redraw walks top down at
;  about 360 T a row against the beam's 224, so once the beam is
;  in front it stays in front and the gap only widens. Waiting for
;  it to clear the top row of the window is enough.
;
;    blit writes window row i at   25,536 + 1,456 + i*360
;    beam reads screen row 48+i at 25,088 + i*224
;    difference                    1,904 + i*136, positive throughout
;
;  That is worth 16,128 T against waiting for the whole window,
;  which is what brings a drag back inside a single frame.
;
;  Time is counted in scan lines, not T states. One line is
;  exactly 224 T and the top border is exactly 64 lines, so the
;  target is an 8 bit add rather than a division. The delay loop
;  below is tuned to 224 T a turn, so the count is the number of
;  lines to wait and nothing has to be scaled.
;
;  PREWORK is how far into the frame we already are when this is
;  entered: the interrupt path, the heartbeat, the three pointer
;  routines and the test that got us here. Measured, not guessed.
; ------------------------------------------------------------
PREWORK         equ     22              ; scan lines already spent, rounded down
                                        ; so the wait errs one line long, which
                                        ; puts the safety at the front where a
                                        ; mistake would tear.

WaitBeamTopOfWin:
                ld      a,(WinY)
                add     a,66 - PREWORK  ; border, the row itself, two lines spare
                ret     c               ; already past it, do not wait
                ret     z
                ld      c,a
                ld      hl,WaitPad      ; a harmless uncontended read
WbLine:
                ld      b,15            ; 7
WbIn:
                djnz    WbIn            ; 13*15 - 5 = 190
                ld      a,(hl)          ; 7
                nop                     ; 4
                dec     c               ; 4
                jr      nz,WbLine       ; 12, so 224 T a line
                ret
WaitPad:        defb    0

IFDEF DEMO
; ------------------------------------------------------------
;  Demo drag
;  Drives the window up and down with no input at all, so the
;  scheduler can be tested without keystroke injection and the
;  result is reproducible. Built with --equ DEMO=1, and paired
;  with --equ NOWAIT=1 to build the same drag with the beam
;  scheduler removed. The two captures are the experiment.
; ------------------------------------------------------------
DemoStep:
                ld      a,(WinX)
                ld      (WinOldX),a
                ld      a,(WinY)
                ld      (WinOldY),a
                ld      b,a
                ld      a,(DemoDir)
                add     a,b
                ld      (WinY),a
                cp      100
                jr      c,DsLow
                ld      a,-2
                ld      (DemoDir),a
                jr      DsSet
DsLow:
                cp      16
                jr      nc,DsSet
                ld      a,2
                ld      (DemoDir),a
DsSet:
                ld      a,1
                ld      (WinMoved),a
                ret
DemoDir:        defb    2
ENDIF

; ------------------------------------------------------------
;  Status line
; ------------------------------------------------------------
; ------------------------------------------------------------
;  ShowStatus
;  Cost 23,008 T for twenty characters and was suppressed during
;  a drag rather than fixed, which is why the drop count used to
;  be invisible at exactly the moment frames were being dropped.
;  It cost an hour of a mouse hunt too: a press on a title bar
;  starts a drag, the row froze before it could paint the L, and
;  the button looked like it had never registered at all.
;
;  Almost nothing on the row changes between frames. So build
;  what it should say into StatRow, compare that against
;  StatShown, which is what is on the glass, and paint only the
;  cells that differ. A still pointer changes nothing and costs
;  the compare alone; a moving one changes a digit or two. The
;  eight ClearRow calls go with it: every cell is written from
;  the buffer, spaces included, so there is nothing to clear.
;
;  StatShown starts as zeros rather than spaces, so the first
;  frame differs everywhere and paints the whole row once.
; ------------------------------------------------------------
ShowStatus:
                ; Rebuilding the row costs 3,808 T whether anything
                ; changed or not, measured as STATB, and most frames
                ; change nothing at all. So compare the eight bytes the
                ; row is made of before building it: eight bytes against
                ; thirty two characters, and on a match there is nothing
                ; to build and nothing to paint.
                call    SsChanged
                ret     z

                call    SsBuild
                ld      hl,StatRow
                ld      de,StatShown
                ld      c,0
SsDiff:
                ld      a,(de)
                cp      (hl)
                jr      z,SsSame
                ld      a,(hl)
                ld      (de),a          ; glass and shadow agree again
                push    hl
                push    de
                push    bc
                ld      b,STATROW
                call    PrintChar
                pop     bc
                pop     de
                pop     hl
SsSame:
                inc     hl
                inc     de
                inc     c
                ld      a,c
                cp      SCRCOLS
                jr      c,SsDiff
                ret

; Have any of the eight values the row is made of moved since it
; was last painted? Returns Z if not, and remembers them if so.
; They are scattered rather than contiguous, so this is written
; out rather than being an LDIR over a struct.
SsChanged:
                ld      hl,SsWas
                ld      c,0
                ld      a,(PtrX)
                call    SsCmp
                ld      a,(PtrY)
                call    SsCmp
                ld      a,(Buttons)
                call    SsCmp
                ld      a,(MouseOn)
                call    SsCmp
                ld      a,(JoyOn)
                call    SsCmp
                ld      a,(FrameCnt)
                call    SsCmp
                ld      a,(Dropped)
                call    SsCmp
                ld      a,(Dropped+1)
                call    SsCmp
                ld      a,c
                or      a
                ret

; A = the value now, HL = its slot. Counts a difference in C,
; remembers the new value, and steps HL on.
SsCmp:
                cp      (hl)
                jr      z,SsCmpSame
                ld      (hl),a
                inc     c
SsCmpSame:
                inc     hl
                ret

; The parts of the row that never change, written once. Most of
; it is constant: the field letters, the spaces between them, and
; the machine name, which cannot change after DetectMachine. Doing
; that every frame cost an LDIR and seven stores for nothing, and
; the blank is not needed either, because every variable field
; below writes a definite character into every cell it owns.
SsInit:
                ld      hl,StatRow
                ld      de,StatRow+1
                ld      bc,31
                ld      (hl),' '
                ldir
                ld      a,'X'
                ld      c,0
                call    SsPut
                ld      a,'Y'
                ld      c,6
                call    SsPut
                ld      a,'B'
                ld      c,12
                call    SsPut
                ld      a,'K'
                ld      c,19
                call    SsPut
                ld      a,'D'
                ld      c,24
                call    SsPut
                ld      a,(Is128)
                or      a
                ld      hl,Txt48
                jr      z,SsiMach
                ld      hl,Txt128
SsiMach:
                ld      c,27            ; 26 put 48K hard against the D field
                call    SsStr
                ld      a,1
                ld      (SsReady),a
                ret

; What the row should say, into StatRow. Nothing here touches the
; screen, so it costs the same whether anything changed or not.
SsBuild:
                ld      a,(SsReady)
                or      a
                call    z,SsInit
                ld      a,(PtrX)
                ld      c,2
                call    SsDec3
                ld      a,(PtrY)
                ld      c,8
                call    SsDec3

                ; buttons are active low, show L or R when held
                ld      a,(Buttons)
                bit     1,a
                ld      a,'.'
                jr      nz,SsbNoL
                ld      a,'L'
SsbNoL:
                ld      c,14
                call    SsPut
                ld      a,(Buttons)
                bit     0,a
                ld      a,'.'
                jr      nz,SsbNoR
                ld      a,'R'
SsbNoR:
                ld      c,15
                call    SsPut

                ld      a,(MouseOn)
                or      a
                ld      a,'.'
                jr      z,SsbNoM
                ld      a,'M'
SsbNoM:
                ld      c,17
                call    SsPut
                ld      a,(JoyOn)
                or      a
                ld      a,'.'
                jr      z,SsbNoJ
                ld      a,'J'
SsbNoJ:
                ld      c,18
                call    SsPut
                ld      a,(FrameCnt)
                ld      c,21
                call    SsDec3

                ; A3, at a glance. A single saturating digit is all the
                ; row has room for and all the question needs: any figure
                ; but nought means this machine has dropped a frame since
                ; it booted. The exact total is in Dropped, which is where
                ; the bench and the harness read it.
                ld      hl,(Dropped)
                ld      a,h
                or      a
                ld      a,9
                jr      nz,SsbDrop
                ld      a,l
                cp      10
                jr      c,SsbDrop
                ld      a,9
SsbDrop:
                add     a,'0'
                ld      c,25
                jp      SsPut

; A = character, C = column.
SsPut:
                push    hl
                ld      hl,StatRow
                ld      b,0
                add     hl,bc
                ld      (hl),a
                pop     hl
                ret

; HL = zero terminated string, C = column.
SsStr:
                ld      b,0
                ld      de,StatRow
                ex      de,hl
                add     hl,bc
                ex      de,hl                   ; DE into the row, HL the text
SsStrLoop:
                ld      a,(hl)
                or      a
                ret     z
                ld      (de),a
                inc     hl
                inc     de
                jr      SsStrLoop

; A = value, C = column. Three digits, the same shape as
; PrintDec3 but into the buffer rather than onto the screen.
SsDec3:
                ld      d,0
SsD100:
                cp      100
                jr      c,SsD100d
                sub     100
                inc     d
                jr      SsD100
SsD100d:
                push    af
                ld      a,d
                add     a,'0'
                call    SsPut
                inc     c
                pop     af
                ld      d,0
SsD10:
                cp      10
                jr      c,SsD10d
                sub     10
                inc     d
                jr      SsD10
SsD10d:
                push    af
                ld      a,d
                add     a,'0'
                call    SsPut
                inc     c
                pop     af
                add     a,'0'
                jp      SsPut


; ------------------------------------------------------------
;  Text
; ------------------------------------------------------------
TxtMenu:        defb    "ZX DESK   FILE   VIEW   HELP",0
TxtX:           defb    "X",0
TxtY:           defb    "Y",0
TxtB:           defb    "B",0
Txt48:          defb    "48K ",0
Txt128:         defb    "128K",0
TxtWinTitle:    defb    "NOTES",0

; ------------------------------------------------------------
;  Graphics
; ------------------------------------------------------------

; pointer: 8 shifts, each 11 rows of data0,mask0,data1,mask1
PtrShiftTab:
                defw    PtrSh0, PtrSh1, PtrSh2, PtrSh3, PtrSh4, PtrSh5, PtrSh6, PtrSh7

PtrSh0:
                defb    $00,$80,$00,$00
                defb    $00,$C0,$00,$00
                defb    $40,$E0,$00,$00
                defb    $60,$F0,$00,$00
                defb    $70,$F8,$00,$00
                defb    $78,$FC,$00,$00
                defb    $7C,$FE,$00,$00
                defb    $7E,$FF,$00,$00
                defb    $78,$FF,$00,$00
                defb    $58,$FC,$00,$00
                defb    $00,$CC,$00,$00

PtrSh1:
                defb    $00,$40,$00,$00
                defb    $00,$60,$00,$00
                defb    $20,$70,$00,$00
                defb    $30,$78,$00,$00
                defb    $38,$7C,$00,$00
                defb    $3C,$7E,$00,$00
                defb    $3E,$7F,$00,$00
                defb    $3F,$7F,$00,$80
                defb    $3C,$7F,$00,$80
                defb    $2C,$7E,$00,$00
                defb    $00,$66,$00,$00

PtrSh2:
                defb    $00,$20,$00,$00
                defb    $00,$30,$00,$00
                defb    $10,$38,$00,$00
                defb    $18,$3C,$00,$00
                defb    $1C,$3E,$00,$00
                defb    $1E,$3F,$00,$00
                defb    $1F,$3F,$00,$80
                defb    $1F,$3F,$80,$C0
                defb    $1E,$3F,$00,$C0
                defb    $16,$3F,$00,$00
                defb    $00,$33,$00,$00

PtrSh3:
                defb    $00,$10,$00,$00
                defb    $00,$18,$00,$00
                defb    $08,$1C,$00,$00
                defb    $0C,$1E,$00,$00
                defb    $0E,$1F,$00,$00
                defb    $0F,$1F,$00,$80
                defb    $0F,$1F,$80,$C0
                defb    $0F,$1F,$C0,$E0
                defb    $0F,$1F,$00,$E0
                defb    $0B,$1F,$00,$80
                defb    $00,$19,$00,$80

PtrSh4:
                defb    $00,$08,$00,$00
                defb    $00,$0C,$00,$00
                defb    $04,$0E,$00,$00
                defb    $06,$0F,$00,$00
                defb    $07,$0F,$00,$80
                defb    $07,$0F,$80,$C0
                defb    $07,$0F,$C0,$E0
                defb    $07,$0F,$E0,$F0
                defb    $07,$0F,$80,$F0
                defb    $05,$0F,$80,$C0
                defb    $00,$0C,$00,$C0

PtrSh5:
                defb    $00,$04,$00,$00
                defb    $00,$06,$00,$00
                defb    $02,$07,$00,$00
                defb    $03,$07,$00,$80
                defb    $03,$07,$80,$C0
                defb    $03,$07,$C0,$E0
                defb    $03,$07,$E0,$F0
                defb    $03,$07,$F0,$F8
                defb    $03,$07,$C0,$F8
                defb    $02,$07,$C0,$E0
                defb    $00,$06,$00,$60

PtrSh6:
                defb    $00,$02,$00,$00
                defb    $00,$03,$00,$00
                defb    $01,$03,$00,$80
                defb    $01,$03,$80,$C0
                defb    $01,$03,$C0,$E0
                defb    $01,$03,$E0,$F0
                defb    $01,$03,$F0,$F8
                defb    $01,$03,$F8,$FC
                defb    $01,$03,$E0,$FC
                defb    $01,$03,$60,$F0
                defb    $00,$03,$00,$30

PtrSh7:
                defb    $00,$01,$00,$00
                defb    $00,$01,$00,$80
                defb    $00,$01,$80,$C0
                defb    $00,$01,$C0,$E0
                defb    $00,$01,$E0,$F0
                defb    $00,$01,$F0,$F8
                defb    $00,$01,$F8,$FC
                defb    $00,$01,$FC,$FE
                defb    $00,$01,$F0,$FE
                defb    $00,$01,$B0,$F8
                defb    $00,$01,$00,$98

; pointer height 11, block size 44 bytes, total 368 bytes

; ------------------------------------------------------------
;  Variables
; ------------------------------------------------------------
Is128:          defb    0
MouseOn:        defb    0
MouseLX:        defb    0
MouseLY:        defb    0
NewMX:          defb    0
NewMY:          defb    0
JoyOn:          defb    0
Dirs:           defb    0
HoldCnt:        defb    0
Speed:          defb    1
FrameCnt:       defb    0
FrX:            defb    0
FrY:            defb    0
FrW:            defb    0
FrH:            defb    0
IrqCnt:         defw    0               ; A3: interrupts the machine took. A
                                        ; word because INC IX is the only
                                        ; increment that leaves the flags alone
Dropped:        defw    0               ; frames the main loop never saw
IrqSaveIX:      defw    0

FrPat:          defw    0
FrWHalf:        defb    0
FrSaveSP:       defw    0
FrWord:         defw    0

; A2. Row nought owes nothing, so its payment has to go somewhere.
; Here, and it is discarded.
FrIrqPad:       defs    2
FrOwed:         defw    0               ; the row still owed its leftmost pair
FdOwed:         defw    0               ; its pattern rides in A'
DeskY:          defb    0
DeskH:          defb    0
PxRow:          defb    0
PrintInv:       defb    0
; ------------------------------------------------------------
;  B4: the window record
;  WinX, WinY, WinW and WinH are referenced ninety three times
;  across six files. Making them fields of a struct that every
;  caller indexes through would be ninety three edits and a
;  slower system. So this stays the live copy and WndSelect swaps
;  records in and out of it, which is nought edits and one LDIR.
;
;  That is the same trick D3 used for the panel record, and it
;  works here for the same reason: a system with one of
;  something, that needs several, is usually better served by
;  keeping the one and swapping the rest than by making every
;  reader indirect.
; ------------------------------------------------------------
WinRec:
WinX:           defb    8
WinY:           defb    48
WinW:           defb    18              ; the same as AppNote's, because
WinH:           defb    72              ; window nought is a notepad and a
                                        ; scroll bar takes a column
WinTitle:       defw    TxtWinTitle
WinBufP:        defw    0               ; where this window's pixels live,
                                        ; allocated from the heap by WndInit
WinApp:         defw    AppNote         ; who owns this window, and where
WinStateP:      defw    0               ; that owner keeps this instance's
                                        ; state. F2 replaced a WinKind that
                                        ; WinDraw and HdlKey each had to
                                        ; know the whole list of
WinOldX:        defb    8
WinOldY:        defb    48
WinMoved:       defb    0
WNDRECSZ        equ     $-WinRec

; F1 turned this from a declaration into a budget. It was two because
; two buffers were declared at fixed addresses; the buffers come from
; the heap now, sized to their windows, so the number is whatever the
; heap will carry. Four is the record table's cost, which is 13 bytes
; each, against a heap that holds seven windows of the notepad's size.
WNDMAX          equ     4

WndTab:         defs    WNDMAX*WNDRECSZ
WndZ:           defs    WNDMAX          ; window indices, front of the z order
WndCount:       defb    0               ; first
WndCur:         defb    0
Dragging:       defb    0
DragDX:         defb    0
DragDY:         defb    0
SsReady:        defb    0               ; has the fixed part been written
SsWas:          defs    8               ; the inputs as the row last saw them
StatRow:        defs    SCRCOLS         ; what the status row should say
StatShown:      defs    SCRCOLS         ; and what is on the glass. Zeros, so
                                        ; the first frame paints all of it
Buttons:        defb    $FF
KeyDirs:        defb    0
PtrX:           defb    120
PtrY:           defb    90
PtrOldX:        defb    120
PtrOldY:        defb    90
DecVal:         defb    0
TmpY:           defb    0

; Three acceleration ramps, pixels per frame as the hold builds.
; SetApply points AccelPtr at one of them; ReadInput never knows
; which. A digital device needs a ramp either way, because one
; fixed rate is either twitchy for placing the pointer or glacial
; for crossing the screen.
ACCELLEN        equ     5
AccelTabs:
                defb    1,1,2,2,3       ; 0, slow: for placing things
                defb    1,2,3,5,7       ; 1, the measured default
                defb    2,3,5,7,11      ; 2, fast: 11 crosses 248 in 23 frames

PtrSave:        defs    PTRH*2
ScrTab:         defs    SCRROWS*2

; INCLUDE is preprocessed, so it cannot sit inside an IF. The
; file guards its own contents with IFDEF BENCH instead.
                include "bench3.asm"

; The slow region is assembled last and placed first. Last, because
; DEFS needs its sizes already known and PTSIZE is panel.inc's, which
; is included above; an ORG that goes backwards is nothing to pasmo,
; which lays the image out by address rather than by order.
                org     SLOWORG
SlowStart:
                ; The file layer. Nothing in it runs inside a frame: a read
                ; or a write is a menu item or a key, and the tape and the
                ; printer hold DI for their own timing anyway.
                include "storage.inc"

                include "bank.inc"

                include "tape.inc"

                include "esxdos.inc"

                include "esxtest.inc"

                include "printer.inc"

                include "dlgset.inc"

                include "dialog.inc"

                include "calendar.inc"

IFDEF BENCH
                include "settings.inc"
ENDIF

                include "filemgr.inc"

                include "loader.inc"

                include "dsksetup.inc"

                include "commander.inc"

                include "arrange.inc"

                include "shortcut.inc"

                include "scroll.inc"

                ; F3's staging area. It used to be the printer buffer at
                ; $5B00 and the printer wants that back.
BKSTAGE:        defs    BKSTAGESZ
SlowEnd:

                end     Main
