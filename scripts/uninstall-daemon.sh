#!/usr/bin/env bash
# Remove xpanel-daemon. Run with sudo. Settings in /var/lib/xpanel are kept.
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "Run with sudo."; exit 1; }
systemctl disable --now xpanel-daemon.service 2>/dev/null || true
rm -f /etc/systemd/system/xpanel-daemon.service /usr/local/bin/xpanel-daemon
systemctl daemon-reload
echo "xpanel-daemon removed. To use hhd again: sudo systemctl enable --now hhd@\$SUDO_USER"
