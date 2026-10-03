; ============================================================
;  Calendar
;  A month grid, and like the clock it has no idea what the date
;  is until it is told. What it does know is which day of the
;  week any date falls on, and that is the whole of the
;  arithmetic worth having on a machine with no calendar chip.
;
;  Day of the week is worked out by counting forward from a known
;  anchor rather than by Zeller or Sakamoto, because both of
;  those want division by 4, 100 and 400, and this has neither a
;  divide instruction nor a reason to want one. 1 January 1980
;  was a Tuesday. A year is 365 days, which is 1 modulo 7, or 366
;  in a leap year, which is 2, so walking a century is at most a
;  hundred adds of one or two. It happens once per repaint.
;
;  The century rule never fires. This covers 1980 to 2079 and the
;  only century year in that span is 2000, which is a leap year
;  because it divides by 400, so within this range a leap year is
;  exactly a year that divides by four. That is not a
;  simplification being got away with, it is a property of the
;  range, and the range is stated so the next person can check
;  it.
;
;  Weeks start on Monday, which is the British convention and the
;  one a Sinclair machine should have.
;
;  The month on show is per window, so two calendars can be open
;  on two months. The date that is highlighted is not: today is
;  the machine's, the same way the time is, and midnight moves it
;  through CalNextDay.
; ============================================================
CALW            equ     23              ; 21 characters of grid and two borders
CALH            equ     82
CALYEAR0        equ     1980
CALYEARS        equ     100
CALCELL         equ     3               ; characters to a day
CALSTSZ         equ     3               ; the year, the month and the day
                                        ; the keys are pointing at

; The per instance state, live copy. Two bytes, and the smallest
; application state in the system, which is the point of it being a
; block rather than a special case.
CalState:
CalYear:        defb    0               ; years since CALYEAR0
CalMonth:       defb    0               ; 0 to 11
CalSel:         defb    1               ; the day the keys are on, 1 to 31

; ------------------------------------------------------------
;  CalInit
;  A new calendar opens on whatever today is.
; ------------------------------------------------------------
CalInit:
                ld      a,(TodayY)
                ld      (CalYear),a
                ld      a,(TodayM)
                ld      (CalMonth),a
                ld      a,(TodayD)
                ld      (CalSel),a
                ret

; CalLeapY, for the year on show. Everything downstream wants it and
; working it out in two places is how the two copies disagree.
CalSetLeap:
                ld      a,(CalYear)
                call    CalLeap
                ld      a,0
                jr      nz,CslNo
                inc     a
CslNo:
                ld      (CalLeapY),a
                ret

; The selection, forward and back a day, carrying into the next month
; and the next year. Moving a week is this seven times, which is not
; elegant and is about forty T states, and a second routine that got
; the month boundary subtly differently would be worse.
CalFwd:
                call    CalSetLeap
                ld      a,(CalMonth)
                call    CalMonthLen
                ld      b,a
                ld      a,(CalSel)
                inc     a
                cp      b
                jr      z,CfSet
                jr      c,CfSet
                ld      a,1
                ld      (CalSel),a
                ld      hl,CalMonth
                inc     (hl)
                ld      a,(hl)
                cp      12
                ret     c
                ld      (hl),0
                ld      hl,CalYear
                inc     (hl)
                ld      a,(hl)
                cp      CALYEARS
                ret     c
                dec     (hl)                    ; the end of the range, so it
                ld      a,11                    ; stops rather than wrapping
                ld      (CalMonth),a            ; into arithmetic it cannot do
                ld      a,31
CfSet:
                ld      (CalSel),a
                ret

CalBack:
                ld      a,(CalSel)
                dec     a
                jr      z,CbPrevMonth
                ld      (CalSel),a
                ret
CbPrevMonth:
                ld      a,(CalMonth)
                or      a
                jr      nz,CbSameYear
                ld      a,(CalYear)
                or      a
                ret     z                       ; the start of the range
                dec     a
                ld      (CalYear),a
                ld      a,12
CbSameYear:
                dec     a
                ld      (CalMonth),a
                call    CalSetLeap
                ld      a,(CalMonth)
                call    CalMonthLen
                ld      (CalSel),a
                ret

; B times, whichever way.
CalMoveB:
                push    bc
                ld      a,c
                or      a
                jr      z,CmbBack
                call    CalFwd
                jr      CmbNext
CmbBack:
                call    CalBack
CmbNext:
                pop     bc
                djnz    CalMoveB
                ret

; ------------------------------------------------------------
;  CalLeap
;  in:  A = years since 1980
;  out: Z set if it is a leap year
; ------------------------------------------------------------
CalLeap:
                and     3                       ; 1980 divides by four, so the
                ret                             ; offset does too

; ------------------------------------------------------------
;  CalMonthLen
;  in:  A = month 0..11, CalLeapY = whether the year is a leap
;  out: A = days in it
; ------------------------------------------------------------
CalMonthLen:
                ld      e,a
                ld      d,0
                ld      hl,CalLens
                add     hl,de
                ld      a,(hl)
                ld      b,a
                ld      a,e
                cp      1                       ; February
                ld      a,b
                ret     nz
                ld      a,(CalLeapY)
                or      a
                ld      a,b
                ret     z
                inc     a
                ret

CalLens:        defb    31,28,31,30,31,30,31,31,30,31,30,31

; ------------------------------------------------------------
;  CalFirstDow
;  in:  CalYear, CalMonth
;  out: A = weekday of the 1st, Monday nought
;  Leaves CalLeapY set for the displayed year, because everything
;  that follows wants it and working it out twice is how the two
;  copies disagree.
; ------------------------------------------------------------
CalFirstDow:
                call    CalSetLeap
                ld      a,1                     ; 1 Jan 1980 was a Tuesday
                ld      (CalDow),a
                xor     a
                ld      (CalIx),a
CfdYear:
                ld      a,(CalIx)
                ld      hl,CalYear
                cp      (hl)
                jr      z,CfdMonths
                call    CalLeap                 ; A is still CalIx
                ld      b,1
                jr      nz,CfdNotL2
                inc     b                       ; 366 is 2 modulo 7
CfdNotL2:
                ld      a,(CalDow)
                add     a,b
                call    CalMod7
                ld      (CalDow),a
                ld      hl,CalIx
                inc     (hl)
                jr      CfdYear
CfdMonths:
                xor     a
                ld      (CalIx),a
CfdMonth:
                ld      a,(CalIx)
                ld      hl,CalMonth
                cp      (hl)
                ret     z                       ; A is dropped; CalDow is the
                                                ; answer and the caller reads
                                                ; it back below
                call    CalMonthLen
                ld      b,a
                ld      a,(CalDow)
                add     a,b
                call    CalMod7
                ld      (CalDow),a
                ld      hl,CalIx
                inc     (hl)
                jr      CfdMonth

; A modulo 7, for a value that cannot be far past it.
CalMod7:
                cp      7
                ret     c
                sub     7
                jr      CalMod7

; ------------------------------------------------------------
;  CalNextDay
;  Midnight. Rolls today's date, and the month lengths it needs
;  are the ones for today's year rather than the displayed one,
;  so it works them out for itself.
; ------------------------------------------------------------
CalNextDay:
                ld      a,(TodayY)
                call    CalLeap
                ld      a,0
                jr      nz,CndNotLeap
                inc     a
CndNotLeap:
                ld      (CalLeapY),a
                ld      a,(TodayM)
                call    CalMonthLen
                ld      b,a
                ld      a,(TodayD)
                inc     a
                cp      b
                jr      z,CndSet                ; the last day is still this
                jr      c,CndSet                ; month
                ld      a,1
                ld      (TodayD),a
                ld      hl,TodayM
                inc     (hl)
                ld      a,(hl)
                cp      12
                ret     c
                ld      (hl),0
                ld      hl,TodayY
                inc     (hl)
                ld      a,(hl)
                cp      CALYEARS
                ret     c
                ld      (hl),0                  ; 2080 is outside what the day
                ret                             ; of the week walk covers
CndSet:
                ld      (TodayD),a
                ret

; ------------------------------------------------------------
;  CalDraw
;  F2's draw vector. A header, a row of weekday letters and six
;  week rows, each built into one string and printed with one
;  call, because PrintStrPx2 costs 622 T a character however it
;  is asked and twenty one characters in one string is twenty one
;  characters.
; ------------------------------------------------------------
CalDraw:
                call    CalFirstDow
                ld      a,(CalDow)
                ld      (CalFirst),a
                ld      a,(CalMonth)
                call    CalMonthLen
                ld      (CalLen),a

                ; the header: three letters of the month and four of year
                ld      hl,CalRow
                ld      a,(CalMonth)
                ld      b,a
                add     a,a
                add     a,b                     ; three characters a name
                ld      e,a
                ld      d,0
                push    hl
                ld      hl,CalNames
                add     hl,de
                pop     de
                ld      bc,3
                ldir                            ; DE is left after the name
                ex      de,hl
                ld      (hl),' '
                inc     hl
                ld      a,(CalYear)
                ld      b,a
                ld      a,CALYEAR0/100          ; 19, and the century of the
                ld      c,a                     ; range rather than of the year
                ld      a,CALYEAR0-1900         ; 80
                add     a,b
                cp      100
                jr      c,CalCent
                sub     100
                inc     c
CalCent:
                ld      b,a
                ld      a,c
                ld      c,0                     ; the year keeps its noughts
                push    bc                      ; and B is not kept in B:
                call    CalPut2                 ; CalPut2 builds its tens digit
                pop     bc                      ; there, so the year came out
                ld      a,b                     ; as 2050, which is the ASCII
                ld      c,0                     ; of the digit it had just
                call    CalPut2                 ; printed
                ld      (hl),0
                ld      b,3
                call    CalPrint

                ; the weekday letters
                ld      hl,CalDays              ; LDIR copies (HL) to (DE), so
                ld      de,CalRow               ; the source is the labels and
                ld      bc,21                   ; the destination is the row.
                ldir                            ; The other way round wrote the
                                                ; header over the labels and
                                                ; printed the header twice
                ld      b,12
                call    CalPrint

                ; and six weeks
                xor     a
                ld      (CalIx),a
CalWeek:
                call    CalBuildWeek
                ld      a,(CalIx)
                add     a,a
                add     a,a
                add     a,a                     ; eight pixel rows a week
                add     a,21
                ld      b,a
                call    CalPrint
                ld      hl,CalIx
                inc     (hl)
                ld      a,(hl)
                cp      6
                jr      c,CalWeek
                call    CalToday
                jp      CalSelDraw

; The grid row for week CalIx, into CalRow.
CalBuildWeek:
                ld      hl,CalRow
                xor     a
                ld      (CalCol),a
CalCellLoop:
                ; the day number this cell holds, which can be off either
                ; end of the month and is a pair of spaces when it is
                ld      a,(CalIx)
                ld      b,a
                add     a,a
                add     a,a
                add     a,a
                sub     b                       ; times seven
                ld      b,a
                ld      a,(CalCol)
                add     a,b
                ld      b,a                     ; the cell's index in the grid
                ld      a,(CalFirst)
                ld      d,a                     ; D, not C: CalPut2 wants C
                ld      a,b
                sub     d
                jr      c,CalBlank              ; before the first
                inc     a
                ld      b,a
                ld      a,(CalLen)
                cp      b
                jr      c,CalBlank              ; past the last
                ld      a,b
                ld      c,1                     ; a day drops its leading zero
                call    CalPut2
                jr      CalCellDone
CalBlank:
                ld      (hl),' '
                inc     hl
                ld      (hl),' '
                inc     hl
CalCellDone:
                ld      (hl),' '
                inc     hl
                ld      a,(CalCol)
                inc     a
                ld      (CalCol),a
                cp      7
                jr      c,CalCellLoop
                dec     hl                      ; the trailing space is not
                ld      (hl),0                  ; wanted, and 21 characters is
                ret                             ; what the window is sized for

; A = a value below 100, HL = where its two digits go, C = 1 to print
; a leading nought as a space. Days want that, because a calendar
; reading 07 is a calendar written by a computer at you; the year does
; not, because 20 5 is not a year.
CalPut2:
                ld      b,'0'-1
CalP2T:
                inc     b
                sub     10
                jr      nc,CalP2T
                add     a,10
                ld      e,a
                ld      a,b
                cp      '0'
                jr      nz,CalP2Put
                ld      a,c
                or      a
                ld      a,'0'
                jr      z,CalP2Put
                ld      a,' '
CalP2Put:
                ld      (hl),a
                inc     hl
                ld      a,e
                add     a,'0'
                ld      (hl),a
                inc     hl
                ret

; CalRow at row B of the window, in the first column.
CalPrint:
                ld      c,0
                ld      hl,CalRow
                jp      ApiWinPrint

; The cell for day A of the month on show: returns B = its row and
; C = its column, both relative to the window. The caller has already made sure the
; day is in this month.
CalCellAt:
                dec     a
                ld      b,a
                ld      a,(CalFirst)
                add     a,b
                call    CalDiv7                 ; A = week, CalRem = column
                add     a,a
                add     a,a
                add     a,a
                add     a,21
                ld      d,a
                ld      a,(CalRem)
                ld      b,a
                add     a,a
                add     a,b                     ; three columns to a cell
                ld      c,a
                ld      b,d
                ret

; The selected day, printed again inverted. In a one bit display
; inversion is the only emphasis there is, so it goes to the thing the
; keys are pointing at rather than to today: today is marked instead
; with a rule under it, which is two bytes of screen and reads as an
; underline rather than as a second kind of selection.
CalSelDraw:
                ld      a,(CalSel)
                call    CalCellAt
                push    bc
                ld      a,(CalSel)
                ld      hl,CalPair
                ld      c,1
                call    CalPut2
                ld      (hl),0
                pop     bc
                ld      hl,CalPair
                jp      ApiWinPrintInv

CalToday:
                ld      a,(TodayY)
                ld      hl,CalYear
                cp      (hl)
                ret     nz
                ld      a,(TodayM)
                ld      hl,CalMonth
                cp      (hl)
                ret     nz
                ld      a,(TodayD)
                call    CalCellAt
                ld      a,b
                add     a,7                     ; the last row of the glyph
                ld      b,a
                call    ApiWinAddr
                ld      (hl),$FF
                inc     hl
                ld      (hl),$FF
                ret

; A divided by seven. Returns A = the quotient, CalRem = the rest.
CalDiv7:
                ld      c,0
CalD7:
                cp      7
                jr      c,CalD7Done
                sub     7
                inc     c
                jr      CalD7
CalD7Done:
                ld      (CalRem),a
                ld      a,c
                ret

; ------------------------------------------------------------
;  CalKey
;  F2's key vector. Left and right walk the months, up and down
;  the years, and both stop at the ends of the range the day of
;  the week walk covers rather than wrapping into arithmetic it
;  cannot do.
; ------------------------------------------------------------
CalKey:
                cp      KEY_LEFT
                jr      nz,CkyNotL
                ld      b,1
                ld      c,0
                jr      CkyMove
CkyNotL:
                cp      KEY_RIGHT
                jr      nz,CkyNotR
                ld      b,1
                ld      c,1
                jr      CkyMove
CkyNotR:
                cp      KEY_UP
                jr      nz,CkyNotU
                ld      b,7
                ld      c,0
                jr      CkyMove
CkyNotU:
                cp      KEY_DOWN
                jr      nz,CkyNotD
                ld      b,7
                ld      c,1
CkyMove:
                call    CalMoveB
                jr      CkyPaint
CkyNotD:
                cp      KEY_ENTER
                ret     nz
                ; The only way the machine has of being told the date, and
                ; the clock rolls it over from here at midnight.
                ld      a,(CalYear)
                ld      (TodayY),a
                ld      a,(CalMonth)
                ld      (TodayM),a
                ld      a,(CalSel)
                ld      (TodayD),a
CkyPaint:
                call    ApiPtrRestore
                call    ApiWinClear
                call    CalDraw
                call    ApiWndBar               ; it has no bar, so this is a
                                                ; compare and a return, and it
                                                ; is here so the rule is the
                                                ; same in every application
                call    ApiWinGrab
                call    ApiPtrSaveBg
                jp      ApiPtrDraw

CalNames:       defb    "JANFEBMARAPRMAYJUNJULAUGSEPOCTNOVDEC"
; Two letters a day, so the labels sit over the digits rather than
; beside them. Three character cells and a space between: twenty
; characters, which is what a week row is.
CalDays:        defb    "MO TU WE TH FR SA SU",0

TodayY:         defb    0               ; today is the machine's, not the
TodayM:         defb    0               ; window's, the same way the time is
TodayD:         defb    1

CalLeapY:       defb    0
CalDow:         defb    0
CalFirst:       defb    0
CalLen:         defb    0
CalIx:          defb    0
CalCol:         defb    0
CalRem:         defb    0
CalPair:        defb    0,0,0
CalRow:         defs    24
