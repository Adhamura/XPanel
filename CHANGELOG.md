# Changelog

## 1.7.0

- **CPU and GPU clock control (Linux).** Two new Quick Panel tiles, **CPU Clock** and **GPU Clock**.
  Ⓐ opens a pop-up with a **Mode** row: **Auto** (the driver decides), **Range** (you set the minimum
  and maximum) or **Fixed** (one exact clock). ◀ ▶ change the values in 100 MHz steps. A lower CPU
  maximum keeps the CPU cooler; a higher GPU minimum stops the GPU from idling at 800 MHz. The same
  limits are in Settings › Performance (GPU minimum clock, CPU maximum / minimum clock). The daemon
  writes `cpufreq` `scaling_min_freq` / `scaling_max_freq` on every core and the amdgpu
  `pp_od_clk_voltage` range, keeps them in its state and applies them again at start and after a
  CPU Boost change. Add the tiles with Ⓨ (Edit). Not on Windows.
- **Lock boost to SPL.** Settings › Performance › Manual and the Quick Panel's Manual TDP tile: SPPT
  and FPPT equal SPL, so the chip never draws more than the set watts (without it they are SPL + 5 W
  and + 10 W, which is why the SoC power could read above the Manual TDP).
- **Manual TDP tile pop-up.** Ⓐ on the tile opens SPL (◀ ▶, 1 W steps) and the boost lock, instead of
  stepping +2 W on each press.
- **Real-time Monitor shows clocks.** CPU (average of all cores) and GPU clock in MHz next to load and
  temperature.
- **Fixes.** The daemon no longer crashes when it stops while the app is connected. The ◀ ▶ arrows in
  the Quick Panel pop-ups now each have their own touch / click area (before, a click on ◀ could raise
  the value).
- **Update note.** The new settings need the new daemon. With the AppImage run
  `sudo ./XPanel-*-x86_64.AppImage --install` again; from source, `sudo scripts/install-daemon.sh`.

## 1.6.1

- **Memory Assigned to GPU on Linux.** Settings › Performance › GPU Settings now also works on
  Linux. It reads and writes the BIOS UMA size through the `asus-armoury` kernel driver's `apu_mem`
  attribute (Auto, 1 to 8 GB on the Ally X) and applies it at the next boot, as on Windows. It needs a
  kernel with `asus-armoury` (6.19+, or CachyOS / Bazzite kernels); without it the row stays hidden.
- **Linux fixes.** The charging state follows any mains supply, not only one named `AC0`. Device Info
  shows the board name. The Game Library also finds Steam in `~/.steam/steam` and the Flatpak.
- **Docs.** README screenshots of the Control Mode key map (front, top and back) in test mode, made by
  `tools/screenshots/capture.sh` with scripted pad input. The Windows notes are up to date with 1.6.

## 1.6.0

Windows catches up with Linux: tested on a ROG Ally X with Windows 11 24H2.

- **Visual key map.** Control Mode shows the ROG Ally X as line art (front, top edge and back, redrawn
  from the ASUS manual) with one view at a time and a 3D flip between them. Tap a button on the
  drawing (or pick it with the D-pad) and assign it in the side panel; every button shows its current
  action. **Test mode** lights up the buttons you hold, moves a dot with each stick and fills the
  triggers as you pull them, on Windows and Linux. **Swap back paddles** if your paddles report the
  other way round.
- **Windows controller layer.** Button remaps are programmed into the Ally's controller (MCU) itself,
  like ASUS does it: no virtual pad, no double input in games. Stick and trigger curves, dead zones,
  gyro and the second function while a paddle is held use a virtual Xbox 360 pad (ViGEmBus, optional;
  XPanel says when games may see two controllers). Desktop Mode drives mouse and keyboard. The gyro
  comes from Windows' motion sensor. XPanel turns the built-in controller on at every start, so a
  controller switched off by other software comes back by itself. The two menu buttons, both back
  paddles and the ROG long press work; the panel can be driven with the pad. HidHide is not used: it
  hides the Ally pad from every program (nefarius/HidHide#178).
- **Windows Update Center.** BIOS, MCU firmware and drivers for your model from ASUS's support API,
  compared with what is installed, sorted (updates first, then not installed). **Install** downloads
  from `dlcdnets.asus.com`, checks ASUS's SHA-256 and runs the official installer with a Windows
  permission prompt; BIOS and MCU need the charger, at least 20 % battery and a confirmation.
  **Install all driver updates** does one after another.
- **Windows system controls.** Volume and mute, Wi-Fi and Bluetooth (tiles and the Connection page:
  scan, connect, pair), audio devices and microphone, power menu, touch keyboard, refresh rate, display
  mode, screenshot, show desktop, brightness for the built-in screen and external monitors (DDC/CI,
  or software dimming when a monitor does not answer DDC/CI on Windows), real-time monitor with CPU and
  GPU temperature (AMD ADL), fan speed, load and power.
- **Windows hardware.** Memory Assigned to GPU (BIOS setting, takes effect after a restart), a separate
  **CPU Boost** switch, fan **Max** now also spins at idle (Linux too), Device Info and battery health
  read from Windows, Game Library from Steam and the Start Menu with per-game profiles, Media Gallery
  open / show in folder / move to the Recycle Bin, user name and picture, storage, your own keyboard
  shortcuts, diagnostics for Windows.
- **Everywhere.** Pages get a scroll bar you can drag; new dropdown control; the avatar is round.
- Third-party code: AMD Display Library SDK headers (`third_party/adl/`, MIT) and ViGEmClient
  (`third_party/vigem/`, MIT).

## 1.5.0

- **Feedback goes to GitHub.** **Settings › Feedback** (and a "Report a problem" row in
  **Settings › General**) now writes a report and opens a pre-filled GitHub issue (Idea: feature
  request form, Problem: bug report form, Other: plain issue) in your browser. You submit it with your
  own GitHub account; XPanel sends nothing and carries no GitHub token. The report has a title, your
  text and, with **Include device info** (on by default and shown before it is sent), the XPanel
  version, model, BIOS, desktop, distro, kernel, Qt version and language, never a serial number,
  user name or path. A **QR code** of the same link lets you file it from a phone (Steam Game Mode has
  no browser to speak of); when the text is too long for a QR code, the code opens the issue with title
  and device info only and the full text goes to the clipboard. **Copy** copies the report as text,
  **Save locally** keeps the old notes file. The QR code is made by
  [qrcodegen](https://github.com/nayuki/QR-Code-generator) (Project Nayuki, MIT), bundled in
  `third_party/qrcodegen/`.
- **XPanel updates itself.** Settings › Update Center has a new **XPanel** section: installed and
  latest version, release notes, **Check now**, **Update now**, **Skip this version** and
  **Check for updates automatically** (on by default: one request to `api.github.com` a minute after
  the start and then once a day; a new version shows up as a tray message, in the User Center and
  as a dot on Settings and on the Quick Panel's gear). Updates are downloaded from the GitHub release and checked against
  its `SHA256SUMS` before anything is installed. The installed AppImage (`/opt/xpanel`) updates
  through `pkexec <AppImage> --install` (or shows the `sudo` command when there is no password
  prompt, e.g. in Steam Game Mode), a portable AppImage saves the new file next to itself, Windows
  runs the new Setup silently and starts XPanel again; release archives, packages and source builds
  link the release page. **Restart XPanel** switches to the new version. `src/AppUpdate.*`,
  `src/UpdateLogic.h` (unit-tested). The AppImage installer now restarts `xpanel-daemon` on
  every `--install` and recognizes the user under `pkexec`; the Windows installer starts XPanel
  again after an update.

## 1.4.0

- **Translations.** Every text in the Quick Panel, Control Center and tray menu can be translated
  (`qsTr` / `tr`), now in **10 languages**: English, German, Spanish, French, Italian, Japanese,
  Korean, Brazilian Portuguese, Russian and Simplified Chinese. The language follows the system, or pick
  one in **Settings › General › Language** (applies at once, tray menu included); the choice is
  saved as `language` in the config. The `i18n/xpanel_<code>.ts`
  files (de, es, fr, it, ja, ko, pt_BR, ru, zh_CN) are compiled into the
  program (`qt_add_translations`, resources `:/i18n/xpanel_<code>.qm`), nothing extra to install.
  Build needs Qt Linguist tools, otherwise XPanel builds in English only; releases and CI build
  with `-DXPANEL_REQUIRE_TRANSLATIONS=ON`. Maintainers: target `update_translations`.
  Values sent to xpanel-daemon and stored in the config (modes, effects, key names) stay English.

## 1.3.0

- **GNOME and Steam Game Mode (experimental).** XPanel now finds out which desktop it runs in
  (`src/Desktop.*`: KDE Plasma, GNOME, gamescope or other, from `XDG_CURRENT_DESKTOP`,
  `GAMESCOPE_WAYLAND_DISPLAY` and the session name; `XPanel --desktop` prints it,
  `XPANEL_DESKTOP=` overrides it) and hides what a desktop cannot do instead of showing buttons
  that do nothing. KDE Plasma behaves as before.
  - **GNOME:** the panel runs on XWayland (Mutter has no layer-shell), full screen and kept above.
    Brightness of the internal screen through logind (`SetBrightness`, `/sys/class/backlight`),
    external monitors through `ddcutil` when installed. Colour presets through Night Light
    (always on), the Keyboard tile switches GNOME's screen keyboard, Screenshot uses
    `gnome-screenshot`, Log out asks GNOME's session manager, Edit profile opens GNOME Settings.
    Refresh rate, Display Mode, wide colour, Show Desktop and the KDE shortcut settings are hidden.
  - **Steam Game Mode (gamescope):** the Quick Panel, Control Center and Real-time Monitor are
    gamescope external overlays (X11 property `GAMESCOPE_EXTERNAL_OVERLAY`, one window at a time),
    drawn over the game and driven with the pad through xpanel-daemon. No tray icon; brightness
    through logind; KDE / desktop-only tiles hidden. New systemd user unit `xpanel.service`
    (installed to `lib/systemd/user`, not enabled by default) starts XPanel in Game Mode only
    (`ExecCondition=XPanel --is-game-mode`): `systemctl --user enable xpanel.service`, or the
    AppImage's `--install --game-mode` / `scripts/install.sh --game-mode`.
  - Other desktops: logind brightness, generic launchers.
  - Terminals (Update Center) and the file manager (Media Gallery, BIOS download) follow the
    desktop: Konsole / Dolphin on KDE, Console / Ptyxis / GNOME Terminal and
    `org.freedesktop.FileManager1` (Nautilus …) elsewhere, `x-terminal-emulator` / `xdg-open` as
    fallbacks. App Diagnostics shows the detected desktop.
  - Build: optional libxcb (`libxcb1-dev`, CMake option `XPANEL_WITH_XCB`) for the gamescope overlay.
  - Not yet tested on real GNOME or gamescope sessions with an Ally: please report how it works.
- **ROG Xbox Ally (RC73YA, Ryzen Z2 A) and ROG Xbox Ally X (RC73XA, Ryzen AI Z2 Extreme)**:
  experimental, untested on hardware, please report. Detected by DMI board name (model ids
  `xboxally` / `xboxallyx`), on Linux and Windows.
  - Operating Modes (SPL): Xbox Ally Silent 10 W, Performance 15 W, Turbo 20 W; Xbox Ally X
    Silent 13 W, Performance 17 W, Turbo 25 W on battery and 35 W on the charger (ASUS).
  - Power limit ranges: the Xbox Ally X uses the kernel's `asus-armoury` ranges (SPL 7–35,
    SPPT 13/14–45, FPPT 19–55 W); the Xbox Ally, which `asus-armoury` does not list yet, the
    Ryzen Z2 A's 6–20 W.
  - Controller: the same USB device as the Ally X (`0b05:1b4c`), so the input layer and Aura
    lighting work as there. New button codes: holding the left (Xbox) button (`KEY_F21`) and the
    right button (`KEY_F22`); back buttons as `KEY_F14` / `KEY_F15` (also on the keyboard node)
    are the paddles.
- The daemon never takes a USB product id 0 as the controller.
- Tests: `tst_model` (model detection, controller ids, table consistency).

## 1.2.0

- **Gyro**: the Ally's motion sensor (BMI323, kernel IIO driver) as a **mouse** or added to the
  **right stick** for aiming, set per mode in Control Mode › Gyro: sensitivity per axis, turning
  axis Yaw or Roll, Invert horizontal / vertical, "Only while a button is held" (LT). Not yet
  tested on hardware, please report how it feels and whether the directions are right.
  - The daemon reads the sensor through the IIO buffer (200 Hz, the sensor's own trigger) or by
    polling at 125 Hz, only while gyro is on and a game has the pad, and restores the sensor's
    settings when it stops. If another program (hhd, InputPlumber) holds it, gyro shows "busy".
  - Gyro calibration (Settings › Calibration or Control Mode): ~1 second at rest; the offset is
    saved by the daemon (`gyroBias`). New daemon request `{"input": {"calibrate": "gyro"}}`;
    new state keys `gyro` (no device / ready / active / busy), `gyroMethod`, `gyroCalibration`.
  - systemd unit: `DeviceAllow=char-iio rw` for the sensor.
- **Custom fan curve editor** (Settings › Performance › Fan Mode › Custom): an 8-point graph of
  fan speed (%) over temperature (°C). Drag the points with touch or mouse, or use the gamepad:
  Ⓐ to edit, ◀ ▶ pick a point, ▲ ▼ fan speed, LB / RB temperature, Ⓑ or Ⓐ done. Moving a point
  moves its neighbours along so the curve never falls. Presets Quiet, Balanced and Performance
  as a starting point, one curve for both fans or a separate GPU curve, the current CPU
  temperature and fan speed shown next to the graph. Changes are applied a moment after you let go.
  - Daemon: new fan mode `custom` and key `fanCurve` (`{"cpu": [[°C, %] x 8], "gpu": [...]}`, `gpu`
    optional), checked strictly (8 points, 20..105 °C rising, 0..100 % never falling), saved with the
    other settings and put back after TDP changes. Linux writes the 8 points to the hwmon
    `asus_custom_fan_curve` (% converted to 0..255); Windows sends them through the ATKACPI fan
    curve devices in the same byte layout as Quiet and Max.
  - The Quick Panel fan tile and the per-game Fan Mode include Custom (the tile once a curve is
    saved).
- **More Aura effects**: Dual breathing (between two colors), Rainbow direction (right / left /
  up / down), Strobe, Battery level (red to green with the charge, updated every 5 %) and Follow
  Operating Mode (Silent blue, Performance white, Turbo red, Manual your color). New daemon keys
  `lightColor2` and `lightDirection`; the effects are in the Quick Panel lighting tile too.
  **The new effects are not yet tested on hardware**; Off, Static, Breathing, Color cycle and
  Rainbow send exactly the same bytes as before.
- Unit tests for the fan curve checks and the Aura packets (`tests/`).
- Screenshot tool: the fake daemon can start from another state (`XPANEL_FAKE_STATE`) and pass on
  scripted gamepad events.

## 1.1.0

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
- Brightness follows the active display: external monitor only, Ally only, or one bar per screen when extended.
- Per-game profiles now apply when a game starts from Steam (or from XPanel) and are restored
  when it ends. Settings you change during the game are kept; the Quick Panel shows
  "Profile: <game>" while one is active. Game Options gain Fan Mode and a "Use this profile when
  the game starts from Steam" switch.
- Unit tests (`ctest`), run in CI on Debian and Arch.

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
