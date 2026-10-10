# Changelog

## Unreleased

**Security**: only root and members of the new group `xpanel` can use the daemon socket
(`root:xpanel 0660`, checked again with `SO_PEERCRED` / `SO_PEERGROUPS`). Before, any local
program could change TDP and fan mode, ask for the controller grab and read the serial number.
`install-daemon.sh` and the Arch package create the group and add the user who ran `sudo`
(log out and in once); the uninstall paths remove it. The systemd unit is now sandboxed
(`ProtectSystem=strict`, device allow list, capability bounding set) and no longer uses the
deprecated `systemd-udev-settle.service`.

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

**Fixed**: the daemon dropped requests over 4 KB, so large Control Mode settings were silently
not applied; it now accepts up to 128 KB (the setting itself is limited to 64 KB).

Known limits: Ally X only (the original Ally is untested, its input layer is not matched yet);
KDE Plasma desktop session only (no Steam Game Mode / gamescope); gyro settings are saved but
not applied; fan control is Auto / Quiet / Max presets.
