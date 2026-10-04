; ============================================================
;  E4: settings
;  A small record persisted through the storage layer, so it
;  works on whatever backend is selected. On a stock 48K that is
;  tape; on a machine with an interface it will be the same code
;  talking to a different backend.
;
;  The record carries a magic byte and a version so a settings
;  file from an older build, or a tape block that happens to have
;  the right name, is rejected rather than believed. Anything
;  that fails the check falls back to defaults, which is the only
;  safe behaviour when the alternative is applying rubbish to the
;  pointer and losing control of the machine.
; ============================================================
; Version 2 added SetKeyPtr and changed the default of SetInvertY
; from 0 to 1. The old default was never the working value: the
; pointer read a separate MouseInvY that defaulted to 1, so a
; version 1 record replayed into version 2 code would invert the Y
; axis of a machine that had been behaving. A version bump rejects
; it and falls back, which is what the field is for.
SETMAGIC        equ     $5A             ; 'Z'
; Three, because the record grew a sound setting. An older file is
; rejected and the defaults stand, which is what the version field has
; been carrying since it was written and the first time it has earned
; its byte.
SETVERSION      equ     3
SETSIZE         equ     8

SetDefaults:
                ld      a,SETMAGIC
                ld      (SetRec),a
                ld      a,SETVERSION
                ld      (SetRec+1),a
                ld      a,1             ; pointer speed, middle of three
                ld      (SetSpeed),a
                ld      a,1             ; the counter rises as the mouse is
                ld      (SetInvertY),a  ; pushed away, which is the common wiring
                ld      a,1
                ld      (SetLattice),a
                ld      a,1             ; keyboard pointer, behind EXTEND MODE
                ld      (SetKeyPtr),a
                ld      a,1             ; sound on, because a desktop that
                ld      (SetSound),a    ; has to be told to speak is a
                                        ; desktop nobody knows can
                xor     a
                ld      (SetRec+7),a
                ret

; Carry set means nothing usable was found and defaults are in place.
SetLoad:
                ld      hl,SetName
                ld      b,FA_READ
                call    StOpen
                jr      c,SetlNone
                ld      (SetHandle),a
                ld      hl,SetRec
                ld      bc,SETSIZE
                call    StRead
                ld      (SetGot),bc
                ld      a,(SetHandle)
                call    StClose
                ld      hl,(SetGot)
                ld      a,h
                or      a
                jr      nz,SetlBad
                ld      a,l
                cp      SETSIZE
                jr      nz,SetlBad
                ld      a,(SetRec)
                cp      SETMAGIC
                jr      nz,SetlBad
                ld      a,(SetRec+1)
                cp      SETVERSION
                jr      nz,SetlBad
                or      a
                ret
SetlBad:
SetlNone:
                call    SetDefaults
                scf
                ret

; Carry set if the backend refused. The caller decides what to say
; about it; on tape a refusal usually means nothing was recording.
SetSave:
                ld      a,SETMAGIC
                ld      (SetRec),a
                ld      a,SETVERSION
                ld      (SetRec+1),a
                ld      hl,SetName
                ld      b,FA_OVERWRITE
                call    StOpen
                ret     c
                ld      (SetHandle),a
                ld      hl,SetRec
                ld      bc,SETSIZE
                call    StWrite
                ld      (SetGot),bc
                ld      a,(SetHandle)
                call    StClose
                ld      hl,(SetGot)
                ld      a,h
                or      a
                jr      nz,SetsShort
                ld      a,l
                cp      SETSIZE
                jr      nz,SetsShort
                or      a
                ret
SetsShort:
                ld      a,STERR_FULL
                scf
                ret

; Push the record into the running system. Called after a load and
; after any change, so there is one path from record to behaviour and
; no chance of the two drifting apart.
SetApply:
                ; Every field is clamped before it is used. A record that
                ; passed the magic and version checks can still hold a value
                ; this build has no meaning for, and an out of range index
                ; would read a table entry that is not a table entry.
                ld      a,(SetSpeed)
                cp      3
                jr      c,SetaSpeedOk
                ld      a,1                     ; a corrupt value is not trusted
                ld      (SetSpeed),a
SetaSpeedOk:
                ; SPEED picks one of three acceleration ramps. ReadInput
                ; reads the pointer through AccelPtr, so the choice costs
                ; one indirection per frame and nothing per key.
                ld      e,a
                add     a,a
                add     a,a
                add     a,e                     ; A = index * 5, the ramp length
                ld      e,a
                ld      d,0
                ld      hl,AccelTabs
                add     hl,de
                ld      (AccelPtr),hl

                ld      a,(SetKeyPtr)
                cp      3
                jr      c,SetaKeyOk
                ld      a,1
                ld      (SetKeyPtr),a
SetaKeyOk:

                ; LATTICE picks the desktop background. The pattern bytes
                ; are the immediate operands of the three routines that
                ; paint it, patched here rather than read from a variable,
                ; because two of the three are inner loops that write a
                ; byte every few T states and cannot afford a load.
                ld      a,(SetLattice)
                or      a
                ld      hl,0                    ; plain: both rows blank
                jr      z,SetaLatSet
                ld      hl,(LatDots)            ; L = row 0 byte, H = row 2
SetaLatSet:
                ld      a,l
                ld      (LatA1),a
                ld      (LatA2),a
                ld      (LatA3),a
                ld      a,h
                ld      (LatB1),a
                ld      (LatB2),a
                ld      (LatB3),a
                ret

; The dot lattice as designed: a set pixel every four across on rows
; nought and two of every four, offset by two so the dots sit on a
; diagonal rather than in columns.
LatDots:        defb    $88, $22

SetName:        defb    "SETTINGS",0
SetHandle:      defb    0
SetGot:         defw    0
