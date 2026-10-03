#!/bin/zsh
# ZX Desk: load a .tap into Fuse and capture the emulator window.
#   ./run.sh                       run build/zxdesk.tap
#   ./run.sh build/foo.tap 20      run something else, wait longer to settle
# Quits Fuse politely rather than killing it, and polls for the window
# instead of guessing a sleep, because repeated pkill and a fixed sleep
# left the app running with no window at all.
set -e
ROOT="${0:A:h}"
TAP="${1:-$ROOT/build/zxdesk.tap}"
SETTLE="${2:-10}"

# MACHINE=128 ./run.sh runs on a 128K instead. Fuse reads this at
# launch and, with autosavesettings on, writes it back at quit, so the
# old value is put back afterwards rather than left changed.
FUSEDOM=net.sourceforge.fuse-for-macosx.Fuse
OLDMACHINE=""
if [[ -n "${MACHINE:-}" ]]; then
  OLDMACHINE="$(defaults read "$FUSEDOM" machine 2>/dev/null || echo 48)"
fi

osascript -e 'tell application "Fuse" to quit' 2>/dev/null || true
for i in {1..20}; do
  pgrep -f "/Applications/Fuse.app/Contents/MacOS/Fuse" >/dev/null || break
  sleep 0.5
done

# Wake the display and hold it awake for the run. A locked or slept
# session tears down the window, so System Events reports no window and
# screencapture has nothing to grab. This is not hypothetical.
if [[ -n "$OLDMACHINE" ]]; then
  defaults write "$FUSEDOM" machine -string "$MACHINE"
fi

caffeinate -u -t 1
caffeinate -di -t $((SETTLE + 90)) &
CAFF=$!

# The machine setting is put back on the way out however that happens.
# It was restored only on the success path, and the first time Fuse
# lost focus to another app the guard below exited first and left the
# emulator set to 128.
cleanup() {
  kill $CAFF 2>/dev/null
  if [[ -n "$OLDMACHINE" ]]; then
    osascript -e 'tell application "Fuse" to quit' 2>/dev/null || true
    for i in {1..20}; do
      pgrep -f "/Applications/Fuse.app/Contents/MacOS/Fuse" >/dev/null || break
      sleep 0.5
    done
    defaults write "$FUSEDOM" machine -string "$OLDMACHINE"
  fi
}
trap cleanup EXIT

# A tape passed at a cold launch is not loaded by Fuse 1.10.0, so
# start Fuse first and open the tape once its window is up.
open -a Fuse

BOUNDS=""
for i in {1..30}; do
  BOUNDS=$(osascript -e 'tell application "System Events" to tell process "Fuse" to get {position, size} of window 1' 2>/dev/null) && break
  sleep 1
done
if [[ -z "$BOUNDS" ]]; then
  echo "Fuse came up with no window after 30s. Check the screen for a dialog." >&2
  exit 1
fi

sleep 2
open -a Fuse "$TAP"

sleep "$SETTLE"

# screencapture -R grabs a screen region, not a window, so anything
# sitting on top of that rectangle is what gets captured. Raise Fuse and
# re-read its bounds immediately before the shot.
osascript -e 'tell application "Fuse" to activate' 2>/dev/null || true
sleep 2
BOUNDS=$(osascript -e 'tell application "System Events" to tell process "Fuse" to get {position, size} of window 1' 2>/dev/null)
FRONT=$(osascript -e 'tell application "System Events" to get name of first process whose frontmost is true' 2>/dev/null)
if [[ "$FRONT" != "Fuse" ]]; then
  echo "Fuse is not frontmost (front is $FRONT); capture would show the wrong window." >&2
  exit 1
fi

SHOT="$ROOT/shots/$(date +%H%M%S)-$(basename "${TAP%.tap}").png"
screencapture -x -R "$(echo "$BOUNDS" | tr -d ' ')" "$SHOT"
echo "$SHOT"
