# Changelog

## 1.0.2 — one-file download

- **AppImage**: `XPanel-<version>-x86_64.AppImage` is a single file with everything included
  (Qt, Wayland / X11 plugins, layer-shell-qt). Run it, or install it with
  `sudo ./XPanel-*-x86_64.AppImage --install [--disable-hhd]`: it copies itself to `/opt/xpanel`
  and sets up the root service, the `xpanel` group, the menu entry, autostart and the `xpanel`
  command. `sudo xpanel --uninstall` removes it again.
- `build-release.sh` (and `build-release.bat` via WSL) builds the AppImage next to the archive;
  releases list the files in `SHA256SUMS`.
- The app shows the matching install command (AppImage, `/opt`, or source checkout) when the
  daemon is not running.
- CMake installs the systemd unit and the sysusers file relative to the install prefix.

## 1.0.1 — first public release

**Quick Panel** (Ally button, tray, shortcut): Operating Mode (Silent 13 W / Performance 17 W /
Turbo 25 W, 30 W on the charger / Manual), Control Mode, fan mode, charge limit, lighting,
refresh rate, real-time monitor, brightness and volume sliders, editable tile set.

**Control Center**: Game Library (Steam + installed apps, per-game mode / TDP / control preset
when launched from XPanel), Control Mode (key mapping, paddle-held secondary functions, repeat,
stick and trigger curves, vibration, Desktop mouse mode), Media Gallery, User Center, Settings
(Performance with SPL / SPPT / FPPT, Eco Mode, GPU clock; Update Center; Lighting; Audio; Display;
Connection; Keyboard Shortcuts; Battery Care).

**xpanel-daemon** (root, systemd): TDP and platform profile via asus-armoury, re-applied on charger
plug / unplug and when something else changes it; fan mode; charge limit; Aura lighting over
hidraw; its own input layer (evdev grab + uinput virtual Xbox 360 pad, keyboard and mouse).

**Releases and packaging**: `build-release.sh` (and `build-release.bat` for WSL) builds
`xpanel-<version>-linux-x86_64.tar.gz`; GitHub Actions CI on Debian 13 and Arch Linux; release
workflow on `v*` tags; Arch `PKGBUILD`; CMake install rules for the desktop entry, icon and
systemd unit; `xpanel-daemon --version`.

**Security**: only root and members of the new `xpanel` group may control `xpanel-daemon`
(peer credentials checked on every connection; no new login needed after `usermod -aG xpanel`).
Refused clients get a clear message, which the app shows in Settings. `install-daemon.sh`
creates the group and adds the installing user; packages get a `sysusers.d` entry.
The systemd unit sandboxes the daemon: read-only system except sysfs and its own directories,
only input / hidraw / uinput devices, three capabilities, the `@system-service` system calls
(`systemd-analyze security` exposure 8.9 → 2.2). Hardware write errors now name the reason.

**Fixed**: the daemon dropped requests over 4 KB, so large Control Mode settings were silently
not applied; it now accepts up to 128 KB (the setting itself is limited to 64 KB).

Known limits: Ally X only (the original Ally is untested, its input layer is not matched yet);
KDE Plasma desktop session only (no Steam Game Mode / gamescope); gyro settings are saved but
not applied; fan control is Auto / Quiet / Max presets.
