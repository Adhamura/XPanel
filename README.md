# XPanel

A **Quick Panel** (side panel) and a full **Control Center** for the ASUS ROG Ally / Ally X on Linux.
Our own C++17 / Qt 6 code: the panel app `XPanel` and a small root service `xpanel-daemon`.
No Handheld Daemon (hhd) code or dependency.

> **Not affiliated with ASUS.** XPanel is an independent project. It is not made, endorsed or
> supported by ASUSTeK Computer Inc. ASUS, ROG and ROG Ally are trademarks of ASUSTeK Computer Inc.;
> they are used here only to name the hardware that XPanel supports.
>
> **Use at your own risk.** `xpanel-daemon` runs as root and changes power limits, fan curves,
> the charge limit and the controller input. Firmware files are downloaded from ASUS's own
> servers at run time and are not part of this repository.

## Parts

| Part | Runs as | Does |
|---|---|---|
| `XPanel` (src/, qml/) | user | the panel, tray icon, Ally button, brightness, volume, refresh rate, radios, sensors |
| `xpanel-daemon` (daemon/) | root (systemd) | TDP / Operating Mode, fan curve, charge limit, lighting; saves and restores them |

Hardware interfaces:

- **Operating Mode / TDP:** `/sys/firmware/acpi/platform_profile` and
  `/sys/class/firmware-attributes/asus-armoury/attributes/ppt_pl{1,2,3}_*`.
  Silent 13 W, Performance 17 W, Turbo 25 W (30 W on the charger), Manual (Manual TDP tile).
  The daemon applies the mode again when the charger is plugged / unplugged, or when
  something else changes the TDP.
- **Fan Curve:** hwmon `asus_custom_fan_curve` (Auto = firmware curve, Quiet, Max).
- **Charge Limit:** `BAT0/charge_control_end_threshold`.
- **Lighting:** ASUS Aura HID output reports (report id `0x5A`) on the N-KEY hidraw
  with vendor usage page `0xFF31`.
- **Ally button + Control Mode:** InputPlumber on the system D-Bus. XPanel sets an
  *intercept* on the Quick Panel button (`Gamepad:Button:Guide`) or the Control Center
  button (`Gamepad:Button:QuickAccess`); tray menu → "Open with Ally button". While the
  panel is open, all controller input goes to XPanel (InterceptMode 2), on close back to the
  game (1), on quit it is reset (0). Control Mode sets the InputPlumber virtual controller
  (Xbox Elite, Xbox Series, DualSense Edge, Steam Deck).
- **Desktop:** KDE brightness D-Bus, `wpctl`, `kscreen-doctor`, `nmcli`, `bluetoothctl`, `spectacle`.

Daemon protocol: `/run/xpanel/xpanel.sock`, one JSON object per line:
`{"get":true}`, `{"set":{"opmode":"turbo"}}` → `{"state":{...}}` (also pushed on each change).

## Control Center window

Opens from the **gear icon** (top right of the Quick Panel), **X** in the panel, the tray
right-click menu, a global shortcut (default Meta+Shift+C), or `XPanel --app [page]`.
Sections follow Control Center (without "Content"):

| Section | Contents | Source |
|---|---|---|
| Game Library | Favorites / Game Platforms, search, grid / list, Manage Library, Game Options (per-game mode, TDP, control preset) | Steam appmanifests, `.desktop` games |
| Control Mode | Gamepad / Desktop presets, key mapping (primary + paddle-held secondary), repeat, sticks, triggers, vibration, gyro, external controllers | saved now; applied by the coming XPanel input layer |
| Media Gallery | filter, select, open, share (clipboard), show in folder, delete to trash | `~/Pictures`, `~/Videos` |
| User Center | profile, My Products, notifications | local; no ASUS account on Linux |
| Settings | General, Performance (mode, SPL/SPPT/FPPT, Eco Mode, GPU clock, fan), Update Center, Calibration, Lighting, Audio, Display, Connection, Keyboard Shortcuts, Feedback, Battery Care | xpanel-daemon, PipeWire, NetworkManager, BlueZ, pacman/paru/fwupd, KWin |

Pad: LB / RB section, D-pad move, A select, B back.

## Install

```bash
./run.sh --version                          # build
scripts/install.sh                          # app, tray icon, autostart (user)
sudo scripts/install-daemon.sh --disable-hhd # root service; stops hhd (it changes the same hardware)
```

Run from the folder: `./run.sh` (builds when needed), or double-click `XPanel.desktop`.
Remove: `scripts/uninstall.sh` and `sudo scripts/uninstall-daemon.sh`.

## Controls

| Pad | Keyboard | Action |
|---|---|---|
| Quick Panel button | — | open / close |
| D-pad | Arrows | move |
| A | Enter / Space | change the tile value / select |
| B | Esc | back / close |
| Y | E | Edit Quick Panel |
| LB / RB | PgUp / PgDn | volume |

Tiles change on each press (Silent → Performance → Turbo → Manual → Silent).
Edit the tile set with the small triangle at the top right, or Y. Settings: `~/.config/XPanel/config.json`.

## Notes

- If the controller stops working in games after a crash, reset InputPlumber:
  `busctl --system set-property org.shadowblip.InputPlumber /org/shadowblip/InputPlumber/CompositeDevice0 org.shadowblip.Input.CompositeDevice InterceptMode u 0`
- Watt values per mode are our choice, close to the ASUS defaults on Windows.
- No FPS limiter (no global one on KDE).

## License

MIT. See [LICENSE](LICENSE).
