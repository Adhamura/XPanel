# Changelog

## Unreleased

- **Original ROG Ally (Z1 / Z1 Extreme)**: Operating Modes with the Ally's own wattages, power
  limits from the firmware, controller and buttons. Not yet tested on hardware, please report
  issues.
  - Operating Modes (SPL / SPPT / FPPT): Silent 10 / 13 / 16 W, Performance 15 / 20 / 25 W,
    Turbo 25 / 30 / 35 W on battery and 30 / 35 / 43 W on the charger. The Ally X keeps its values.
  - The model is found by the DMI board name (`RC71…` Ally, `RC72…` Ally X), else by the
    controller's USB id; the daemon reports it as `model` / `modelName`.
  - The Manual SPPT / FPPT sliders use the ranges the firmware reports (new daemon state keys
    `spptMin`/`spptMax`/`fpptMin`/`fpptMax`); the daemon accepts manual values within them.
  - Input layer: the Ally pad (xpad, `0b05:1abe`) and its N-KEY keyboard are found by their USB
    id and capabilities instead of their names, XPanel's own virtual devices are skipped, stick
    and trigger ranges come from the device, and the menu buttons on the N-KEY keyboard
    (`KEY_F16`, `KEY_PROG1`) open the Quick Panel / Control Center.
  - Windows: the power limits and Operating Modes follow the model too.

## 1.0.5

- **Fixed**: the 1.0.4 AppImage crashed at start on Wayland (KDE Plasma, SteamOS / Bazzite desktop
  mode) with `Failed to load client buffer integration: "wayland-egl"`: Qt's Wayland EGL plugin
  was not bundled. It is now, and the release build fails if it is missing.
- If OpenGL cannot start at all (missing or broken GPU driver), XPanel now draws with Qt's
  software renderer instead of aborting.
- The release smoke test also starts the AppImage on Wayland (headless sway with Mesa), not
  only on X11.

## 1.0.4 — Windows (experimental)

- **Windows 10 / 11 x64 installer**: `XPanel-<version>-Windows-x64-Setup.exe`. The same app and
  daemon; on Windows the daemon runs in the user's session (started by XPanel) and uses the ASUS
  System Control Interface (`\\.\ATKACPI`) for Operating Mode / TDP, fan, charge limit, boot sound
  and MCU power save, the Windows HID API for Aura lighting, and the power plan for CPU boost /
  energy preference. Quick Panel: tray icon or Win+Shift+Q; Control Center: Win+Shift+C.
  Not yet on Windows: controller remapping and the Ally buttons, brightness / volume / refresh
  rate, GPU clock. Not tested on hardware yet; please report what works.
- `build-release.bat` builds the Windows installer (`--linux` builds the Linux files in WSL);
  releases publish Linux and Windows files together, each smoke-tested first.

## 1.0.3

- **Fixed**: the 1.0.2 AppImage showed no icons on the Quick Panel tiles: the Qt SVG plugins
  were not bundled (Debian 13 ships them in the separate package `qt6-svg-plugins`). The release build now fails if the SVG, X11 or Wayland / layer-shell
  plugins are missing from the AppImage.
- Releases are automatic: a merge to `main` with a new version in `CMakeLists.txt` builds,
  smoke-tests (the AppImage is started on a virtual screen) and publishes the release.
- The AppImage needs glibc 2.39 or newer (not 2.41 as stated before), so it also runs on
  Ubuntu 24.04 based systems.

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
