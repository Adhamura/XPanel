#!/usr/bin/env bash
# Regenerate the README screenshots in docs/screenshots/ without an Ally:
# XPanel runs on a virtual X server (Xvfb) with demo data and a fake daemon.
#   tools/screenshots/capture.sh [build-dir]       (default: build)
# Needs: xvfb-run, ImageMagick (import, convert), python3 + Pillow, DejaVu fonts.
# As root, a fake battery is bind-mounted over /sys/class/power_supply in a private mount
# namespace (unshare), so the header shows a real-looking charge instead of 0 %.
set -euo pipefail
ROOT=$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd)
TOOLS=$ROOT/tools/screenshots
BIN=$(readlink -f "${1:-$ROOT/build}")/bin/XPanel
OUT=$ROOT/docs/screenshots
[ -x "$BIN" ] || { echo "Build first: $BIN not found"; exit 1; }

if [ "${XPANEL_SHOT_INNER:-}" != 1 ]; then
    WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT
    python3 "$TOOLS/make_demo_home.py" "$WORK/home"
    mkdir -p "$WORK/ps/BAT0" "$WORK/ps/AC0"
    ( cd "$WORK/ps/BAT0"; echo 78 > capacity; echo Discharging > status; echo 14200000 > power_now
      echo 62060000 > energy_now; echo 77000000 > energy_full; echo 80000000 > energy_full_design
      echo 42 > cycle_count; echo 80 > charge_control_end_threshold )
    echo 0 > "$WORK/ps/AC0/online"
    export XPANEL_SHOT_INNER=1 WORK BIN
    run=(xvfb-run -a -s "-screen 0 1920x1080x24" "$0" "$@")
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
trap 'kill $DAEMON 2>/dev/null || true' EXIT
display -window root "$HOME/background.png" & sleep 1
mkdir -p "$OUT"
shot() { import -window root "$WORK/shot.png"; }

"$BIN" --open >/dev/null 2>&1 & sleep 5
shot  # the panel window is transparent outside the panel; there is no compositor, so paste it on the background
convert "$HOME/background.png" \( "$WORK/shot.png" -crop 960x1080+0+0 +repage \) -geometry +0+0 -composite \
    -resize 1600x900 -strip "$OUT/quick-panel.png"
for p in library:game-library control:control-mode gallery:media-gallery settings/performance:performance \
         settings/lighting:lighting settings/battery:battery settings/update:update-center; do
    "$BIN" --app "${p%%:*}" >/dev/null; sleep 4; shot
    convert "$WORK/shot.png" -crop 1600x900+0+0 +repage -strip "$OUT/${p##*:}.png"
done
"$BIN" --quit >/dev/null 2>&1 || true
echo "Screenshots written to $OUT"
