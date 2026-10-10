# Security

`xpanel-daemon` runs as root, so its safety matters. Please report security problems privately
through GitHub: **Security → Report a vulnerability** on this repository (or open an issue that
only says you have something to report, without details).

## What the daemon does and does not do

- It listens only on a local UNIX socket, `/run/xpanel/xpanel.sock`. There is no network listener.
- Only **root and members of the `xpanel` group** may use it. The daemon reads each client's
  credentials (`SO_PEERCRED`) and looks the user up in the group database when it connects, so
  a user added with `usermod -aG xpanel` gets access without a new login, and a removed user
  loses it on the next connection. Everyone else gets `{"error":"denied"}` and is disconnected
  before any state is sent; the refusal is logged once per user in the journal.
  `install-daemon.sh` creates the group and adds the user who ran `sudo`; packages create it
  through `sysusers.d`.
- It accepts a fixed set of keys (operating mode, TDP within 5–40 W, fan mode, charge limit
  50–100 %, lighting, GPU clock, control settings). Each value is validated. It runs no commands
  it receives and writes no paths it receives.
- It writes only to the ASUS firmware attributes, the platform profile, hwmon fan curve,
  `BAT0/charge_control_end_threshold`, the Aura hidraw node, cpufreq, the amdgpu clock files and,
  while gyro is on, the motion sensor's IIO buffer / trigger / sampling rate files (put back when
  gyro stops). It reads the motion sensor (`/dev/iio:deviceN`) and creates a uinput virtual pad.
- The systemd unit sandboxes it (`systemd-analyze security xpanel-daemon` rates it about 2.2,
  "OK", down from 8.9):
  - the file system is read-only except `/sys`, `/dev`, `/proc`, `/var/lib/xpanel` and
    `/run/xpanel` (`ProtectSystem=strict`), and home directories are hidden;
  - only input, hidraw, IIO (motion sensor) and uinput devices can be opened (`DevicePolicy=closed`);
  - root keeps only `CAP_DAC_OVERRIDE`, `CAP_DAC_READ_SEARCH` and `CAP_FOWNER`;
  - only local (UNIX / netlink) sockets, no namespaces, realtime or set-uid files, and the
    `@system-service` system call set.

  Every file, device and system call the daemon uses was checked against this sandbox
  (including an `strace` of a full run). If a setting still fails on your device,
  `journalctl -u xpanel-daemon -b` shows the file and the error; please report it. To find out
  whether the sandbox is the cause, run `sudo systemctl edit xpanel-daemon` and add:

  ```ini
  [Service]
  CapabilityBoundingSet=CAP_SYS_ADMIN CAP_SYS_RAWIO
  DeviceAllow=
  DevicePolicy=auto
  ProtectSystem=no
  SystemCallFilter=
  ```

  then `sudo systemctl restart xpanel-daemon`. Remove it again with `sudo systemctl revert xpanel-daemon`.

## XPanel's own updates

- The update check is one HTTPS request to `api.github.com` (this repository's latest release), a
  minute after the start and then once a day; it can be turned off in Settings › Update Center.
  No token, ID or statistics are sent.
- Downloads are accepted only over HTTPS from `github.com` and `*.githubusercontent.com`; every
  redirect is checked against the same list.
- Before anything is installed, the file's SHA-256 is compared with the release's `SHA256SUMS`.
  A file that does not match, or is not listed, is deleted and not installed. This protects against
  damaged or truncated downloads and a wrong file on the way, **not against a compromised GitHub
  account or release**: the checksums come from the same release as the file (there is no separate
  signature yet).
- XPanel itself never runs as root. The installed AppImage updates through `pkexec` (the system's
  password prompt, polkit) running the new AppImage's own `--install`; on Windows the installer asks
  for administrator rights (UAC). Nothing is installed without that prompt.
- `XPANEL_UPDATE_URL` (a test hook) points the check at another URL; only then are
  `http://127.0.0.1`, `http://localhost` and `file://` addresses accepted as well. Whoever can set
  environment variables for XPanel can run code as that user anyway.

## Known limits (planned work)

- Any program running as a member of the `xpanel` group (in practice: your desktop session)
  can change the power and lighting settings and ask for the controller grab. That is the
  intended trust level, the same as the user at the device.
- BIOS files downloaded by the Update Center come from ASUS's servers over HTTPS and are not
  checked against a separate checksum. XPanel only downloads them; it does not flash them.
