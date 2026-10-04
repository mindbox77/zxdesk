; The settings the frame reads. Kept apart from settings.inc so
; these stay in the fast region when that code does not.
AccelPtr:       defw    AccelTabs+ACCELLEN

SetRec:
                defb    SETMAGIC
                defb    SETVERSION
SetSpeed:       defb    1
SetInvertY:     defb    1
SetLattice:     defb    1
SetKeyPtr:      defb    1
SetSound:       defb    1
                defb    0
