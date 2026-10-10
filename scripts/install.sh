#!/usr/bin/env bash
# Build XPanel and install it for the current user (no root needed).
# - ~/.local/bin/XPanel            the app
# - ~/.local/share/applications/xpanel.desktop + icon
# - ~/.config/autostart/xpanel.desktop  (tray icon at login)
# Works from a source checkout (builds first) and from a release archive (uses bin/XPanel).
# Undo with scripts/uninstall.sh
set -euo pipefail
cd "$(dirname "$0")/.."

if [ -x bin/XPanel ]; then
    APP=bin/XPanel # release archive: prebuilt
else
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DXPANEL_BUILD_TOOLS=OFF
    cmake --build build
    APP=build/bin/XPanel
fi
# --version only loads the libraries: a quick check that Qt 6 is installed.
if ! "$APP" --version >/dev/null 2>&1; then
    echo "XPanel does not start. Missing libraries:"
    ldd "$APP" | grep 'not found' || true
    echo "Install Qt 6 (>= 6.8 for the release archive, >= 6.5 from source): qt6-base qt6-declarative qt6-svg, and layer-shell-qt for the Wayland overlay."
    exit 1
fi

BIN="$HOME/.local/bin"
ICONS="$HOME/.local/share/icons/hicolor/scalable/apps"
mkdir -p "$BIN" "$HOME/.local/share/applications" "$ICONS" "$HOME/.config/autostart"
install -m 644 qml/icons/app.svg "$ICONS/xpanel.svg"
install -m 755 "$APP" "$BIN/XPanel"

cat > "$HOME/.local/share/applications/xpanel.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=XPanel Quick Panel
Comment=Handheld control side panel
Exec=$BIN/XPanel --toggle
Icon=xpanel
Categories=System;Settings;
EOF

cat > "$HOME/.config/autostart/xpanel.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=XPanel
Exec=$BIN/XPanel --background
Icon=xpanel
X-KDE-autostart-phase=2
EOF

# Restart a running XPanel so the new build is used.
"$BIN/XPanel" --quit 2>/dev/null || true
sleep 0.5
setsid -f "$BIN/XPanel" --background >/dev/null 2>&1 < /dev/null || true

echo "Installed. Press the left menu button (left of the screen) to open the XPanel Quick Panel."
[ -S /run/xpanel/xpanel.sock ] || echo "Next: sudo scripts/install-daemon.sh --disable-hhd   (TDP, fan, charge, lighting)"
echo "Tip: bind '$BIN/XPanel --toggle' to a key in KDE System Settings > Shortcuts."
