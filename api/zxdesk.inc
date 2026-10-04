; ZX Desk application interface, version 3.
; Written by mkapi.py. Do not edit.
; Slot n is a jump at $8000 + 3n. Arguments are in src/api.inc.

ApiVersion      equ     $8003   ; 1
ApiWndOpen      equ     $8006   ; 2
ApiWndClose     equ     $8009   ; 3
ApiWndIsFront   equ     $800C   ; 4
ApiWndRepaint   equ     $800F   ; 5
ApiWndBar       equ     $8012   ; 6
ApiWinGrab      equ     $8015   ; 7
ApiPrint        equ     $8018   ; 8
ApiPrintClip    equ     $801B   ; 9
ApiFillRect     equ     $801E   ; 10
ApiAddrAt       equ     $8021   ; 11
ApiPtrRestore   equ     $8024   ; 12
ApiPtrSaveBg    equ     $8027   ; 13
ApiPtrDraw      equ     $802A   ; 14
ApiAlloc        equ     $802D   ; 15
ApiFree         equ     $8030   ; 16
ApiFreeOwner    equ     $8033   ; 17
ApiEvPost       equ     $8036   ; 18
ApiStSelect     equ     $8039   ; 19
ApiStOpen       equ     $803C   ; 20
ApiStClose      equ     $803F   ; 21
ApiStRead       equ     $8042   ; 22
ApiStWrite      equ     $8045   ; 23
ApiStDir        equ     $8048   ; 24
ApiStDelete     equ     $804B   ; 25
ApiStCaps       equ     $804E   ; 26
ApiStIdent      equ     $8051   ; 27
ApiWinRec       equ     $8054   ; 28
ApiWinPrint     equ     $8057   ; 29
ApiWinPrintInv  equ     $805A   ; 30
ApiWinClear     equ     $805D   ; 31
ApiWinAddr      equ     $8060   ; 32
ApiWinRows      equ     $8063   ; 33
ApiWinCols      equ     $8066   ; 34
ApiWinApp       equ     $8069   ; 35
ApiWndCloseNow  equ     $806C   ; 36
ApiKbdMods      equ     $806F   ; 37
ApiWinPrintIf   equ     $8072   ; 38
ApiDlgAlert     equ     $8075   ; 39
ApiDlgConfirm   equ     $8078   ; 40
ApiDlgSave3     equ     $807B   ; 41
ApiOpenFile     equ     $807E   ; 42
ApiWinFill      equ     $8081   ; 43

API_SLOTS       equ     44
API_VER         equ     3
APPSIZE         equ     22
APP_ABOUT       equ     1
APP_CAL         equ     3
APP_CLOCK       equ     2
APP_CLOSE       equ     16
APP_CMD         equ     4
APP_DRAW        equ     6
APP_EXT         equ     6
APP_H           equ     15
APP_INIT        equ     4
APP_KEY         equ     8
APP_KEYS        equ     5
APP_LIVE        equ     2
APP_NOTE        equ     0
APP_SCROLL      equ     18
APP_SCROLLTO    equ     20
APP_STSZ        equ     0
APP_TITLE       equ     10
APP_W           equ     14
APP_X           equ     12
APP_Y           equ     13
FA_OVERWRITE    equ     14
FA_READ         equ     1
KEY_DELETE      equ     8
KEY_DOWN        equ     4
KEY_ENTER       equ     13
KEY_LEFT        equ     1
KEY_RIGHT       equ     2
KEY_UP          equ     3
STCAP_DIR       equ     8
STCAP_PERSIST   equ     2
STCAP_RANDOM    equ     4
STCAP_WRITE     equ     1
STERR_FULL      equ     8
STERR_HANDLE    equ     9
STERR_NAME      equ     7
STERR_NOSUP     equ     10
STERR_NOTFOUND  equ     5
ST_BANK         equ     4
ST_ESXDOS       equ     3
ST_PRINT        equ     8
ST_RAM          equ     1
ST_TAPE         equ     2
WinCapH         equ     10
