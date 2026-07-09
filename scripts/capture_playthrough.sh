#!/usr/bin/env bash
# Full-campaign screenshot harness for the emery (Pebble Time 2) emulator.
#
# A blind button-mash playthrough of a 10-level puzzle crawler is not
# reproducible, so — following microdeck's start-at-floor pattern — we rebuild
# with -DSCREENSHOT_START_MAP=N so a New Game boots straight into level N+1,
# screenshot it, and finally force map 10's VICTORY dialog with -DSCREENSHOT_END.
set -e
export DISPLAY=:99
Xvfb :99 -screen 0 1024x768x24 &>/dev/null &
sleep 2

EM=emery
OUT=/project/reports/playthrough
mkdir -p "$OUT"

SHOT() { pebble screenshot --no-open --emulator $EM "$OUT/$1"; sleep 1; }
TAP()  { pebble emu-button click "$1" --emulator $EM; sleep 0.5; }

build_install() {  # $@ = extra LOP_CFLAGS
  ( cd /project && rm -rf build && \
    LOP_CFLAGS="$*" pebble build >/tmp/build.log 2>&1 ) \
    || { echo "BUILD FAILED ($*)"; tail -40 /tmp/build.log; exit 1; }
  pebble install --emulator $EM >/dev/null
  sleep 5
}

# --- Title screen (clean build) -----------------------------------------
build_install ""
SHOT 00_title.png

# --- Each of the 10 levels ----------------------------------------------
for i in 0 1 2 3 4 5 6 7 8 9; do
  n=$((i + 1))
  echo ">>> Level $n (SCREENSHOT_START_MAP=$i)"
  build_install "-DSCREENSHOT_START_MAP=$i"
  TAP select          # New Game -> warp into level n
  sleep 2
  SHOT "$(printf '%02d' $n)_level${n}.png"
done

# --- Victory screen (map 10 end dialog) ---------------------------------
echo ">>> Victory screen"
build_install "-DSCREENSHOT_START_MAP=9 -DSCREENSHOT_END"
TAP select
sleep 2
SHOT 11_victory_1.png
TAP select; sleep 1; SHOT 12_victory_2.png
TAP select; sleep 1; SHOT 13_victory_3.png

# --- Restore a clean production binary -----------------------------------
echo ">>> Rebuilding clean production binary"
( cd /project && rm -rf build && pebble build >/tmp/build.log 2>&1 )
tail -3 /tmp/build.log

echo "Done. Screenshots in $OUT"
ls -1 "$OUT"
