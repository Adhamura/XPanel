# Security

`xpanel-daemon` runs as root, so its safety matters. Please report security problems privately
through GitHub: **Security → Report a vulnerability** on this repository (or open an issue that
only says you have something to report, without details).

## What the daemon does and does not do

- It listens only on a local UNIX socket, `/run/xpanel/xpanel.sock`. There is no network listener.
- Only root and members of group `xpanel` can use the socket. The socket is `root:xpanel`, mode
  `0660`, and the daemon checks each client again with `SO_PEERCRED` / `SO_PEERGROUPS`. It
  disconnects other clients before it sends them anything (so they do not see the DMI serial
  number either). If the group does not exist, only root can use the socket.
- It accepts a fixed set of keys (operating mode, TDP within 5–40 W, fan mode, charge limit
  50–100 %, lighting, GPU clock, control settings). Each value is validated. It runs no commands
  it receives and writes no paths it receives.
- It writes only to the ASUS firmware attributes, the platform profile, hwmon fan curve,
  `BAT0/charge_control_end_threshold`, the Aura hidraw node, cpufreq and the amdgpu clock files. It creates a uinput
  virtual pad.
- The systemd unit uses `NoNewPrivileges`, `ProtectSystem=strict` (writes only to `/sys`, its
  state and runtime directories), `ProtectHome`, `PrivateTmp`, a device allow list (input,
  hidraw, `/dev/uinput`) and a small capability set (`CAP_CHOWN`, `CAP_DAC_OVERRIDE`,
  `CAP_DAC_READ_SEARCH`, `CAP_FOWNER`).

## Known limits (planned work)

- Every program that runs as a member of group `xpanel` (normally all programs of your desktop
  session) can change the power and lighting settings and ask for the controller grab. The
  daemon cannot tell XPanel from another program of the same user.
- The daemon still runs as root. The sandbox limits what it can write, but it does not limit
  which sysfs files it writes.
- BIOS files downloaded by the Update Center come from ASUS's servers over HTTPS and are not
  checked against a separate checksum. XPanel only downloads them; it does not flash them.
