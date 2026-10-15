#!/bin/zsh
# ZX Desk: assemble a source to a .tap and report the size.
#   ./build.sh              assemble src/zxdesk.asm
#   ./build.sh src/foo.asm  assemble something else
# The --name flag is required: pasmo takes the tape header name from the
# output path as given, so without it the header reads "build/zxde".
set -e
ROOT="${0:A:h}"
export PATH="$ROOT/tools/bin:$PATH"

SRC="${1:-$ROOT/src/zxdesk.asm}"
NAME="$(basename "${SRC%.asm}")"
OUT="$ROOT/build/$NAME.tap"

# BENCH=1 ./build.sh builds the timing harness instead of the desktop.
# DEMO=1, NOWAIT=1 and SCRIPT=1 are the other build flags and were
# documented without being reachable from here, so they had to be run
# by invoking pasmo by hand. DEMO in particular is how A2 gets tested
# against the real ULA: it drags the window by itself, so every frame
# goes through DevFillDesk, and the status row's drop count says
# whether any of those frames lost their interrupt.
# BENCH is the timing harness, which needs Fuse. TEST is the
# correctness subjects, which zxtest.py runs headlessly. They used to
# be one binary and were competing for the same fifteen kilobytes, so
# each now carries only its own half; HARNESS is what both halves'
# shared parts are guarded by.
EQU=()
if [[ -n "${BENCH:-}" ]]; then
  NAME=bench3
  OUT="$ROOT/build/$NAME.tap"
  EQU+=(--equ BENCH=1 --equ HARNESS=1)
elif [[ -n "${TEST:-}" ]]; then
  NAME=zxtest
  OUT="$ROOT/build/$NAME.tap"
  EQU+=(--equ TEST=1 --equ HARNESS=1)
fi
for FLAG in DEMO NOWAIT SCRIPT CLOCKDEMO PRINTDEMO TOURDEMO MOUSETEST ESXTEST; do
  if [[ -n "${(P)FLAG:-}" ]]; then
    EQU+=(--equ "$FLAG=1")
    NAME="$NAME-${FLAG:l}"
    OUT="$ROOT/build/$NAME.tap"
  fi
done

pasmo -I "$ROOT/src" "${EQU[@]}" --name "$NAME.tap" --tapbas \
  "$SRC" "$OUT" "$ROOT/build/$NAME.sym" 2>&1 | grep -v '^WARNING: Var' || true

pasmo -I "$ROOT/src" "${EQU[@]}" --bin "$SRC" "$ROOT/build/$NAME.bin" >/dev/null 2>&1
BYTES=$(stat -f%z "$ROOT/build/$NAME.bin")

# Two regions now. The slow one at $6000 holds code that never runs
# inside a frame; the fast one at $8000 holds everything else and is
# the one bounded by the stack. An overrun of either does not fail
# loudly at run time: the first push lands in the program and the
# machine falls back to BASIC several seconds later with an unrelated
# error, so both are checked here.
#
# The image starts at the lowest ORG, so its length measures from
# there to the top of the fast region and the gap between them is
# zeros on the tape.
CODETOP=$((0xBD00))
SLOWORG=$((0x6000))
SLOWTOP=$((0x8000))
END=$((SLOWORG + BYTES))
SLOWEND=$(awk '$1=="SlowEnd"{v=$3; sub(/[Hh]$/,"",v); print v; exit}' "$ROOT/build/$NAME.sym")
SLOWEND=$(( 16#${SLOWEND:-6000} ))

FAIL=0
if (( END > CODETOP )); then
  printf 'BUILD FAILED: %s ends at $%04X, past CODETOP $%04X by %d.\n' \
    "$NAME" "$END" "$CODETOP" "$((END - CODETOP))" >&2
  FAIL=1
fi
if (( SLOWEND > SLOWTOP )); then
  printf 'BUILD FAILED: %s slow region ends at $%04X, past $%04X by %d.\n' \
    "$NAME" "$SLOWEND" "$SLOWTOP" "$((SLOWEND - SLOWTOP))" >&2
  FAIL=1
fi
if (( FAIL )); then
  rm -f "$OUT"
  exit 1
fi
printf '%s: slow $6000-$%04X, %d free; fast $8000-$%04X, %d free before the stack\n' \
  "$NAME" "$SLOWEND" "$((SLOWTOP - SLOWEND))" "$END" "$((CODETOP - END))"

# The desktop carries the wallpaper band as a second block, loaded to
# $EF00 by the BASIC loader before the jump. pasmo's loader reads one
# block, so taploader.py swaps it for one that reads two and puts the
# band on the end. The harness builds have no desktop and keep
# pasmo's loader.
if [[ -z "${BENCH:-}" && -z "${TEST:-}" ]]; then
  python3 "$ROOT/taploader.py" "$OUT" "$ROOT/src/wallpaper.bin"
fi
echo "$OUT"
