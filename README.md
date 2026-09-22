# WD My Passport Auto-Unlocker for KDE Plasma 6

An automated, native KDE graphical interface to seamlessly unlock hardware-encrypted Western Digital My Passport drives on Linux. Fully rootless at runtime: no system service, no `sudo`.

When you plug your encrypted WD drive into your machine, this tool automatically intercepts the connection, queries the hardware, and pops up a native `kdialog` password prompt in your active user session. A wrong password re-prompts in place (up to 5 attempts). If you dismiss the prompt, the Device Notifier shows an "Unlock WD My Passport" action on the drive entry while it is attached; terminal (`wd-passport-unlocker`) and `systemctl --user start wd-passport-unlocker.service` work too.

> **AUR status:** AUR registration for new accounts is closed since June 2026 (malware cleanup, see `aur-general`). The `PKGBUILD`/`.SRCINFO` in this repo are kept AUR-ready and will be pushed as `wd-passport-kdialog-unlocker` when registration reopens. Until then, install from source or a GitHub Release below.

---

## Features

- **Zero-Click Popup:** Prompt appears automatically the millisecond you plug in the USB cable via `udev` (`SYSTEMD_USER_WANTS`) and a `systemd` user unit — no root involved. Wrong passwords re-prompt in place (5 attempts, cancel anytime).
- **Conditional Device Action:** solid action (`/usr/share/solid/actions/wd-passport-solid-action.desktop`) adds "Unlock WD My Passport" to the Device Notifier drive entry — visible only while a matching USB disk is attached, no permanent icon.
- **CLI Fallback:** `wd-passport-unlocker` in `PATH` — terminal (`wd-passport-unlocker`) or `systemctl --user start wd-passport-unlocker.service`.
- **Themed Prompt:** Everything runs as your user with the full session environment, so `kdialog` follows your KDE theme, fonts, and dark mode.
- **User-Space Backend:** Unlock runs as your user via `uaccess` — no root, no `sudo` dance for the password path.
- **Ultra-Lightweight:** Unlock-only C backend (~22KB) on system `libcrypto`, no Python runtime, no bundled crypto. Frontend is a small `bash` + `kdialog` script.

---

## Architecture Overview

1. **Kernel Event (`udev`):** The system detects a whole-disk block device matching Western Digital's vendor ID (`1058`), grants the active user access via `TAG+="uaccess"`, and asks every user manager to start the unlock prompt via `ENV{SYSTEMD_USER_WANTS}` (`70-wd-passport.rules`). Partitions and the Passport's Virtual CD (`sr0`) are excluded via `ENV{DEVTYPE}=="disk", KERNEL=="sd*"`. The file is numbered `70-` on purpose: the `uaccess` tag must be set before stock `71-seat.rules` (seat assignment) and `73-seat-late.rules` (ACL application) run — a `99-` file is silently too late and no ACL is granted.
2. **Prompt (`bash`/`kdialog`):** The `systemd` user unit runs `wd-unlocker.sh` as your user with the full session environment, so the native KDE `kdialog --password` box follows your theme. Wrong passwords re-prompt in place via `kdialog --warningyesno` (up to `WD_UNLOCKER_MAX_ATTEMPTS`, default 5); cancel/empty exits quietly. Manual retry via the Device Notifier solid action, `wd-passport-unlocker`, or `systemctl --user start wd-passport-unlocker.service` calls the same script. No `loginctl` user-hunting, no `sudo`, no hand-forwarded env vars.
3. **Hardware Handshake (`SG_IO`):** The password is piped over stdin into `wd-unlocker-backend` (`backend/src/`), which sends the status (`C0/45`), Handy-Store (`D8`), and unlock (`C1/E1`) SCSI commands and hashes the password (salt + UTF-16LE + iterated SHA-256 via system OpenSSL) exactly like WD's tool. The kernel filters vendor opcodes without `CAP_SYS_RAWIO`, so the binary ships a file capability (`cap_sys_rawio+ep`, set in `PKGBUILD`) — no `setuid`, no `sudo`; `udev` `uaccess` covers the `open()` side.
4. **Device Matching:** Backends accept both `Western_Digital_My_*` (legacy) and real-HW `WD_My_Passport_*` serials, matched on USB vendor `1058`.

---

## Installation

### From source (Arch Linux)

```bash
git clone https://github.com/yongabyte/wd-passport-plasma-unlocker.git
cd wd-passport-plasma-unlocker
makepkg -si
systemctl --user daemon-reload
```

Unplug and replug the drive once after logging in — the prompt fires on device-add.

### From a GitHub Release

Download the `wd-passport-kdialog-unlocker-*.pkg.tar.zst` asset from Releases, then:

```bash
sudo pacman -U wd-passport-kdialog-unlocker-*.pkg.tar.zst
systemctl --user daemon-reload
```

### Via AUR (when registration reopens)

```bash
yay -S wd-passport-kdialog-unlocker
```

### Dependencies
The package handles installation of all required dependencies automatically:
- `kde-cli-tools` (native `kdialog` support for KDE 6)
- `bash` (frontend script)
- `openssl` (system `libcrypto` for the backend's SHA-256)
- `gcc` (build-time only), `libcap` (for `setcap`, build-time only), `criterion` (test-time only)

The C backend is built from `backend/` during packaging — no separate download, no `sudo pip`, no Python needed.

### Building and testing locally

```bash
make -C backend test   # 20 criterion tests (vectors match upstream Python)
make -C backend        # builds backend/wd-unlocker-backend
printf '%s' "YOUR_PASSWORD" | ./backend/wd-unlocker-backend; echo "exit=$?"
```

Expected without hardware: `no Western Digital Passport device found` (exit 2). With a locked drive attached: exit 0 + `unlocked`.

---

## Manual Configuration & Customization

If your specific external hard drive enclosure uses a non-standard or alternative Western Digital hardware profile, you can manually inspect and adjust your device vendor matching arrays.

### 1. Identifying Your Drive's Vendor ID
Plug in your device and run the following command to verify your hardware properties:
```bash
lsusb
```
Look for your device line (e.g., `Bus 002 Device 004: ID 1058:25ee Western Digital Technologies, Inc.`). The first four digits (`1058`) are the `idVendor`.

### 2. Custom udev Rules
If your drive vendor ID differs from the standard default (`1058`), edit the rules configuration profile located at:
`/usr/lib/udev/rules.d/70-wd-passport.rules`

Update the `ATTRS{idVendor}` property to reflect your exact hardware layout matching tag:
```text
ACTION=="add", SUBSYSTEM=="block", ENV{DEVTYPE}=="disk", KERNEL=="sd*", ATTRS{idVendor}=="YOUR_ID_HERE", TAG+="systemd", TAG+="uaccess", ENV{SYSTEMD_USER_WANTS}+="wd-passport-unlocker.service"
```

Apply your new rule system state instantly by reloading the local kernel environment blocks:
```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
systemctl --user daemon-reload
```

Check the user-unit logs (no root needed):
```bash
journalctl --user -u wd-passport-unlocker.service -f
```

Note: the prompt fires when the device first appears. A drive already plugged in before login won't retrigger — unplug and replug it once after logging in.

---

## Contributing & License

Contributions, issue reports, and feature requests are highly welcome! Feel free to fork the repository and open pull requests.

This project is licensed under the **GPL-3.0-or-later License**. See the `LICENSE` file for details.
