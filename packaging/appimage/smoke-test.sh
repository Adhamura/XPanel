#!/bin/sh
# Start an XPanel AppImage on a virtual X server (Xvfb) and fail on QML or Qt plugin errors,
# e.g. a missing SVG or platform plugin. Used by CI before a release is published.
#   packaging/appimage/smoke-test.sh dist/XPanel-<version>-x86_64.AppImage
# Needs: xvfb-run (packages xvfb, xauth).
set -eu
APP=$(readlink -f "$1")
LOG=$(mktemp)
RUNTIME=$(mktemp -d)
chmod 700 "$RUNTIME"
export APPIMAGE_EXTRACT_AND_RUN=1 XPANEL_BACKEND=x11 QT_QUICK_BACKEND=software XDG_RUNTIME_DIR="$RUNTIME"
# Open the Quick Panel, then three Control Center pages (each loads more QML and icons).
xvfb-run -a -s "-screen 0 1920x1080x24" sh -c '
    "$0" --open & sleep 8
    for p in library settings/performance settings/lighting; do "$0" --app "$p"; sleep 3; done
    "$0" --quit; sleep 1' "$APP" > "$LOG" 2>&1 || true
cat "$LOG"
if grep -E "Unsupported image format|qrc:/.*: (Error|.*is not a type|.*ReferenceError|.*TypeError)|Could not load the Qt platform plugin|cannot open shared object|module .* is not installed" "$LOG"; then
    echo "smoke test FAILED: see the errors above"
    exit 1
fi
grep -q "sent 'app settings/lighting'" "$LOG" || { echo "smoke test FAILED: XPanel did not start"; exit 1; }
echo "smoke test OK"
