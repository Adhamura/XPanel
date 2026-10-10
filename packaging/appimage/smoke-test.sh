#!/bin/sh
# Start an XPanel AppImage and fail on QML, Qt plugin or graphics errors. Used by CI before a
# release is published.
#   packaging/appimage/smoke-test.sh dist/XPanel-<version>-x86_64.AppImage
# 1. X11 on Xvfb (needs xvfb-run).
# 2. Wayland in a headless sway (wlroots, has layer-shell like KDE) with Mesa's software GL,
#    when sway is installed: the panel then uses OpenGL through Qt's wayland-egl plugin, the
#    path that crashed in 1.0.4.
set -eu
APP=$(readlink -f "$1")
FAIL=0
BAD="Unsupported image format|qrc:/.*: (Error|.*is not a type|.*ReferenceError|.*TypeError)|Could not load the Qt platform plugin|cannot open shared object|module .* is not installed|Failed to load client buffer integration|Failed to create RHI|Failed to initialize graphics backend"

# Open the Quick Panel, then three Control Center pages (each loads more QML and icons).
SESSION='"$0" --open & sleep 8
    for p in library settings/performance settings/lighting; do "$0" --app "$p"; sleep 3; done
    "$0" --quit; sleep 1'

check() { # name logfile [extra bad pattern]
    cat "$2"
    if grep -E "$BAD${3:+|$3}" "$2"; then echo "smoke test ($1) FAILED: see the errors above"; FAIL=1; return; fi
    if ! grep -q "sent 'app settings/lighting'" "$2"; then echo "smoke test ($1) FAILED: XPanel did not start"; FAIL=1; return; fi
    echo "smoke test ($1) OK"
}

export APPIMAGE_EXTRACT_AND_RUN=1

LOG=$(mktemp); RUNTIME=$(mktemp -d); chmod 700 "$RUNTIME"
XDG_RUNTIME_DIR="$RUNTIME" XPANEL_BACKEND=x11 QT_QUICK_BACKEND=software \
    xvfb-run -a -s "-screen 0 1920x1080x24" sh -c "$SESSION" "$APP" > "$LOG" 2>&1 || true
check X11 "$LOG"

if command -v sway >/dev/null; then
    LOG=$(mktemp); RUNTIME=$(mktemp -d); chmod 700 "$RUNTIME"
    printf 'output HEADLESS-1 resolution 1920x1080\n' > "$RUNTIME/sway.conf"
    XDG_RUNTIME_DIR="$RUNTIME" WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 \
        sway -c "$RUNTIME/sway.conf" > "$RUNTIME/sway.log" 2>&1 &
    SWAY=$!
    i=0; while [ $i -lt 50 ] && ! ls "$RUNTIME"/wayland-[0-9] >/dev/null 2>&1; do sleep 0.2; i=$((i + 1)); done
    DISPLAY_NAME=$(basename "$(ls "$RUNTIME"/wayland-[0-9] | head -1)")
    # The fallback to software rendering must not be needed here: it would hide a missing plugin.
    env -u DISPLAY XDG_RUNTIME_DIR="$RUNTIME" WAYLAND_DISPLAY="$DISPLAY_NAME" LIBGL_ALWAYS_SOFTWARE=1 \
        sh -c "$SESSION" "$APP" > "$LOG" 2>&1 || true
    kill "$SWAY" 2>/dev/null || true
    check Wayland "$LOG" "OpenGL is not available"
else
    echo "smoke test (Wayland) skipped: sway is not installed"
fi

exit $FAIL
