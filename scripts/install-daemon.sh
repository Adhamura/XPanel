#!/usr/bin/env bash
# Install xpanel-daemon as a system service. Run with sudo:
#   sudo scripts/install-daemon.sh                # install + start
#   sudo scripts/install-daemon.sh --disable-hhd  # also stop and disable Handheld Daemon
# hhd changes the same TDP / fan / lighting settings, so the two must not run together.
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "Run with sudo."; exit 1; }
cd "$(dirname "$0")/.."

if [ -x bin/xpanel-daemon ]; then
    BIN=bin/xpanel-daemon # release archive: prebuilt
elif [ -x build/bin/xpanel-daemon ]; then
    BIN=build/bin/xpanel-daemon
else
    echo "Build first (as your user): ./run.sh --version"
    exit 1
fi

install -m 755 "$BIN" /usr/local/bin/xpanel-daemon
install -m 644 daemon/xpanel-daemon.service /etc/systemd/system/xpanel-daemon.service

if [ "${1:-}" = "--disable-hhd" ]; then
    for u in $(systemctl list-units --all --plain --no-legend 'hhd@*' | awk '{print $1}'); do
        echo "Stopping and disabling $u"
        systemctl disable --now "$u" || true
    done
elif systemctl list-units --plain --no-legend --state=active 'hhd@*' | grep -q hhd; then
    echo "WARNING: Handheld Daemon (hhd) is running. It changes the same settings."
    echo "         Run again with --disable-hhd, or: sudo systemctl disable --now hhd@\$USER"
fi

systemctl daemon-reload
systemctl enable xpanel-daemon.service
systemctl restart xpanel-daemon.service
sleep 1
systemctl --no-pager --lines=5 status xpanel-daemon.service || true
echo "xpanel-daemon installed. Socket: /run/xpanel/xpanel.sock"
