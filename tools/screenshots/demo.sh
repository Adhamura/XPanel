#!/usr/bin/env bash
# Record the README demo (docs/screenshots/demo.gif, ~25 s) without an Ally: XPanel runs on a
# virtual X server with demo data and the fake daemon, demo_input.py plays the gamepad input.
#   tools/screenshots/demo.sh [build-dir]       (default: build)
# Needs what capture.sh needs, plus ffmpeg and xcompmgr (the Quick Panel's dimmed backdrop).
# Also writes demo.mp4 (1280x720) next to the GIF, for posting; it is not committed.
set -euo pipefail
ROOT=$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd)
TOOLS=$ROOT/tools/screenshots
BIN=$(readlink -f "${1:-$ROOT/build}")/bin/XPanel
OUT=$ROOT/docs/screenshots
[ -x "$BIN" ] || { echo "Build first: $BIN not found"; exit 1; }

if [ "${XPANEL_DEMO_INNER:-}" != 1 ]; then
    WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT
    python3 "$TOOLS/make_demo_home.py" "$WORK/home"
    mkdir -p "$WORK/ps/BAT0" "$WORK/ps/AC0"
    ( cd "$WORK/ps/BAT0"; echo 78 > capacity; echo Discharging > status; echo 14200000 > power_now
      echo 62060000 > energy_now; echo 77000000 > energy_full; echo 80000000 > energy_full_design
      echo 42 > cycle_count; echo 80 > charge_control_end_threshold )
    echo 0 > "$WORK/ps/AC0/online"
    export XPANEL_DEMO_INNER=1 WORK BIN
    run=(xvfb-run -a -s "-screen 0 1600x900x24" "$0" "$@")
    if [ "$(id -u)" = 0 ] && command -v unshare >/dev/null; then
        unshare -m sh -c 'mount --bind "$WORK/ps" /sys/class/power_supply && exec "$@"' sh "${run[@]}"
    else
        "${run[@]}"
    fi
    exit
fi

# --- inside Xvfb ---
export HOME=$WORK/home XDG_CONFIG_HOME=$WORK/home/.config XDG_RUNTIME_DIR=$WORK/run
export XPANEL_BACKEND=x11 QT_QUICK_BACKEND=software XPANEL_SOCKET=$WORK/run/daemon.sock
export DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
python3 "$TOOLS/fake_daemon.py" "$XPANEL_SOCKET" & DAEMON=$!
# the "game": a window under XPanel (a root background would vanish behind the compositor)
convert "$HOME/background.png" -resize 1600x900^ -gravity center -extent 1600x900 "$WORK/bg.png"
display -immutable -borderwidth 0 -geometry 1600x900+0+0 "$WORK/bg.png" & GAME=$!
sleep 1
xcompmgr & COMP=$!
trap 'kill $DAEMON $GAME $COMP 2>/dev/null || true' EXIT
"$BIN" --background >/dev/null 2>&1 & sleep 5

ffmpeg -loglevel error -y -f x11grab -draw_mouse 0 -framerate 30 -video_size 1600x900 -i "$DISPLAY" \
    -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p "$WORK/raw.mp4" & REC=$!
sleep 1
python3 "$TOOLS/demo_input.py" "$XPANEL_SOCKET" "$BIN"
kill -INT $REC; wait $REC || true
"$BIN" --quit >/dev/null 2>&1 || true

mkdir -p "$OUT"
ffmpeg -loglevel error -y -ss 1 -i "$WORK/raw.mp4" -vf "scale=1280:720:flags=lanczos" -an \
    -c:v libx264 -preset slow -crf 20 -pix_fmt yuv420p -movflags +faststart "$OUT/demo.mp4"
ffmpeg -loglevel error -y -ss 1 -i "$WORK/raw.mp4" -filter_complex \
    "fps=12,scale=800:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=128[p];[b][p]paletteuse=dither=bayer:bayer_scale=4" \
    "$OUT/demo.gif"
echo "Demo written to $OUT/demo.gif and $OUT/demo.mp4"
