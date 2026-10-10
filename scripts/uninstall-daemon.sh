#!/usr/bin/env bash
# Remove xpanel-daemon. Run with sudo. Settings in /var/lib/xpanel are kept.
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "Run with sudo."; exit 1; }
systemctl disable --now xpanel-daemon.service 2>/dev/null || true
rm -f /etc/systemd/system/xpanel-daemon.service /usr/local/bin/xpanel-daemon
systemctl daemon-reload
# The group only gives access to the daemon socket. groupdel also removes its members.
# Keep it when the distro package (which owns it through sysusers.d) is still installed.
if getent group xpanel >/dev/null && [ ! -e /usr/lib/sysusers.d/xpanel.conf ]; then
    groupdel xpanel
fi
echo "xpanel-daemon removed. To use hhd again: sudo systemctl enable --now hhd@\$SUDO_USER"
