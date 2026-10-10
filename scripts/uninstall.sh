#!/usr/bin/env bash
# Remove XPanel (app, tray autostart, icon) from ~/.local.
set -euo pipefail
BIN="$HOME/.local/bin"
"$BIN/XPanel" --quit 2>/dev/null || true
pkill -x XPanel 2>/dev/null || true
systemctl --user disable xpanel.service 2>/dev/null || true
rm -f "$BIN/XPanel" "$HOME/.local/share/applications/xpanel.desktop" \
      "$HOME/.config/autostart/xpanel.desktop" "$HOME/.config/systemd/user/xpanel.service" "$HOME/.local/share/icons/hicolor/scalable/apps/xpanel.svg"
echo "XPanel removed. For the root part: sudo scripts/uninstall-daemon.sh"
echo " Settings stay in ~/.config/XPanel (delete by hand if you want)."
