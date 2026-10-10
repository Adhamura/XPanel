#!/bin/sh
# Install or remove XPanel from the AppImage (called by AppRun, as root):
#   /opt/xpanel                                  the AppImage contents (app, daemon, Qt)
#   /etc/systemd/system/xpanel-daemon.service    root service (sandboxed, see SECURITY.md)
#   /usr/local/bin/xpanel                        command
#   /usr/local/share/applications, icons         menu entry
#   /etc/xdg/autostart/xpanel.desktop            tray icon at login
# These paths are writable on Arch / CachyOS, Fedora and Bazzite (/opt and /usr/local live in /var).
set -eu
APPDIR="$(cd "$(dirname "$(readlink -f "$0")")/../../.." && pwd)"
DEST=/opt/xpanel
SELF="${APPIMAGE:-$DEST/AppRun}"
ACTION="$1"; shift
DISABLE_HHD=0
for a in "$@"; do [ "$a" = --disable-hhd ] && DISABLE_HHD=1; done

if [ "$(id -u)" != 0 ]; then
    echo "Run with sudo: sudo $SELF $ACTION"
    exit 1
fi
have_systemd() { [ -d /run/systemd/system ]; }

remove_files() {
    rm -f /etc/systemd/system/xpanel-daemon.service /usr/local/bin/xpanel \
          /usr/local/share/applications/xpanel.desktop /etc/xdg/autostart/xpanel.desktop \
          /usr/local/share/icons/hicolor/scalable/apps/xpanel.svg
}

if [ "$ACTION" = --uninstall ]; then
    if have_systemd; then
        systemctl disable --now xpanel-daemon.service 2>/dev/null || true
    fi
    remove_files
    [ "$APPDIR" = "$DEST" ] || rm -rf "$DEST"
    have_systemd && systemctl daemon-reload
    [ "$APPDIR" = "$DEST" ] && rm -rf "$DEST" # last: this script lives there
    echo "XPanel removed. Kept: settings (/var/lib/xpanel, ~/.config/XPanel) and the group xpanel."
    exit 0
fi

echo "==> Installing XPanel to $DEST"
if have_systemd; then
    systemctl stop xpanel-daemon.service 2>/dev/null || true
fi
if [ "$APPDIR" != "$DEST" ]; then
    rm -rf "$DEST.new"
    cp -a "$APPDIR" "$DEST.new"
    rm -rf "$DEST"
    mv "$DEST.new" "$DEST"
fi
# The AppImage mount is private to the user who ran it; the copy must be usable by everyone.
chown -R root:root "$DEST"
chmod -R u+rwX,go+rX,go-w "$DEST"

# Only root and members of the group xpanel may control the daemon.
getent group xpanel >/dev/null || groupadd --system xpanel
if [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != root ]; then
    usermod -aG xpanel "$SUDO_USER"
    echo "    added $SUDO_USER to the group xpanel"
else
    echo "    add your desktop user to the group xpanel: sudo usermod -aG xpanel <user>"
fi

sed "s|^ExecStart=.*|ExecStart=$DEST/usr/bin/xpanel-daemon|" \
    "$DEST/usr/lib/systemd/system/xpanel-daemon.service" > /etc/systemd/system/xpanel-daemon.service
mkdir -p /usr/local/bin /usr/local/share/applications /usr/local/share/icons/hicolor/scalable/apps /etc/xdg/autostart
ln -sf "$DEST/AppRun" /usr/local/bin/xpanel
install -m 644 "$DEST/usr/share/icons/hicolor/scalable/apps/xpanel.svg" /usr/local/share/icons/hicolor/scalable/apps/xpanel.svg
sed "s|^Exec=XPanel |Exec=$DEST/AppRun |" "$DEST/usr/share/applications/xpanel.desktop" \
    > /usr/local/share/applications/xpanel.desktop
cat > /etc/xdg/autostart/xpanel.desktop <<DESKTOP
[Desktop Entry]
Type=Application
Name=XPanel
Exec=$DEST/AppRun --background
Icon=xpanel
X-KDE-autostart-phase=2
DESKTOP

if [ "$DISABLE_HHD" = 1 ] && have_systemd; then
    for u in $(systemctl list-units --all --plain --no-legend 'hhd@*' | awk '{print $1}'); do
        echo "    stopping and disabling $u (it changes the same hardware)"
        systemctl disable --now "$u" || true
    done
elif have_systemd && systemctl list-units --plain --no-legend --state=active 'hhd@*' | grep -q hhd; then
    echo "WARNING: Handheld Daemon (hhd) is running and changes the same settings."
    echo "         Run again with --disable-hhd, or: sudo systemctl disable --now hhd@\$USER"
fi

if have_systemd; then
    systemctl daemon-reload
    systemctl enable --now xpanel-daemon.service
    systemctl --no-pager --lines=3 status xpanel-daemon.service || true
else
    echo "WARNING: systemd is not running; start $DEST/usr/bin/xpanel-daemon as root yourself."
fi

# Start the tray icon for the user who ran sudo (it also starts at every login).
if [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != root ]; then
    sudo -u "$SUDO_USER" setsid -f "$DEST/AppRun" --background >/dev/null 2>&1 < /dev/null || true
fi
echo "Done. Open the Quick Panel with the left Ally button, or start \"XPanel Quick Panel\" from the menu."
echo "Remove with: sudo xpanel --uninstall"
