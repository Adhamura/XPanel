# Security

`xpanel-daemon` runs as root, so its safety matters. Please report security problems privately
through GitHub: **Security → Report a vulnerability** on this repository (or open an issue that
only says you have something to report, without details).

## What the daemon does and does not do

- It listens only on a local UNIX socket, `/run/xpanel/xpanel.sock`. There is no network listener.
- It accepts a fixed set of keys (operating mode, TDP within 5–40 W, fan mode, charge limit
  50–100 %, lighting, GPU clock, control settings). Each value is validated. It runs no commands
  it receives and writes no paths it receives.
- It writes only to the ASUS firmware attributes, the platform profile, hwmon fan curve,
  `BAT0/charge_control_end_threshold`, the Aura hidraw node, cpufreq and the amdgpu clock files. It creates a uinput
  virtual pad.
- The systemd unit uses `NoNewPrivileges`, `ProtectHome` and `PrivateTmp`.

## Known limits (planned work)

- The socket is world-writable. Any local program can change the power and lighting settings, or
  ask for the controller grab. On a single-user handheld this is the same trust level as the
  desktop session, but a group-restricted socket with peer-credential checks is planned.
- BIOS files downloaded by the Update Center come from ASUS's servers over HTTPS and are not
  checked against a separate checksum. XPanel only downloads them; it does not flash them.
