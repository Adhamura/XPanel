#!/usr/bin/env bash
# Build XPanel and install it for the current user (no root needed).
# - ~/.local/bin/XPanel            the app
# - ~/.local/share/applications/xpanel.desktop + icon
# - ~/.config/autostart/xpanel.desktop  (tray icon at login)
# Undo with scripts/uninstall.sh
set -euo pipefail
cd "$(dirname "$0")/.."

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

BIN="$HOME/.local/bin"
ICONS="$HOME/.local/share/icons/hicolor/scalable/apps"
mkdir -p "$BIN" "$HOME/.local/share/applications" "$ICONS" "$HOME/.config/autostart"
install -m 644 qml/icons/app.svg "$ICONS/xpanel.svg"
install -m 755 build/bin/XPanel "$BIN/XPanel"

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
