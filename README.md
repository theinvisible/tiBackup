# tiBackup

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg?style=flat-square)](LICENSE)
[![build-check](https://github.com/theinvisible/tiBackup/actions/workflows/build.yml/badge.svg)](https://github.com/theinvisible/tiBackup/actions/workflows/build.yml)
[![Hosted By: Cloudsmith](https://img.shields.io/badge/OSS%20hosting%20by-cloudsmith-blue?logo=cloudsmith&style=flat-square)](https://cloudsmith.com)

Backup daemon of **tiBackup** — an intelligent, disk-based backup system for Linux
desktops and servers. Plug in a USB disk and a predefined backup job runs
automatically, or schedule jobs daily/weekly/monthly — with rsync, optional LUKS
encryption, pre/post scripts, e-mail notifications and Proxmox Backup Server
integration.

> **This repository** contains `tiBackup`, the background **daemon** (`tibackupd`).
> It runs as **root**, listens for udev/hotplug and scheduled events, performs the
> actual privileged work (mount, rsync, LUKS), and **serves a built-in web UI**
> (HTTP + WebSocket) to manage everything from the browser. See
> [Architecture](#architecture).

## Architecture

tiBackup consists of two parts:

| Component | Role |
|-----------|------|
| **[tiBackupLib](https://github.com/theinvisible/tiBackupLib)** | Shared core library (config, device/partition handling, backup engine). |
| **[tiBackup](https://github.com/theinvisible/tiBackup)** | Background daemon (`tibackupd`), runs as **root**, performs the actual backups and **serves the built-in web UI**. |

The daemon owns all privileged operations and **serves a modern web interface
itself** (Qt `QHttpServer` + `QWebSockets`), calling the library directly
in-process — there is no separate GUI process and no IPC layer. Access is
protected by a login session; live job status streams to the browser over a
WebSocket.

> The former Qt Widgets desktop client (`tiBackupUi`) has been **replaced** by
> this built-in web UI, which removes the unprivileged-client/IPC split entirely.

## Features

- **Hotplug backups** — connect a disk and the matching job starts automatically
- **Scheduled backups** — daily, weekly or monthly
- **rsync-based** incremental file backups, with optional checksum comparison
- **LUKS encryption** support for backup targets
- **Proxmox Backup Server (PBS)** integration
- **E-mail notifications** when a job finishes (SMTP)
- **Pre/post-backup scripts** with dynamic tiBackup variables
- **Built-in web UI** — a modern, browser-based admin interface to manage jobs,
  devices, PBS servers and settings, with **live job status over WebSocket** and a
  native-style file browser; no desktop client required
- **Login-protected**, binds to **localhost by default**, optional **LAN + TLS**
- Simple **INI configuration** under `/etc/tibackup` — usable on headless servers

## Installation

Pre-built packages are published via Cloudsmith for **Debian 13 (trixie)**,
**Ubuntu 24.04 (noble)** and **Ubuntu 26.04**. The setup script auto-detects your
distribution:

```bash
curl -1sLf 'https://dl.cloudsmith.io/public/ti-9x5p/tibackup/setup.deb.sh' | sudo -E bash
sudo apt install tibackup
```

This installs the `tibackupd` systemd service and **enables and starts it
automatically**. Set an admin password and open the web UI in your browser:

```bash
sudo tiBackup --set-web-password          # or use the first-run setup screen
xdg-open http://127.0.0.1:8877            # default bind: localhost, port 8877
```

Check the daemon any time with `systemctl status tibackupd`.

## Configuration & running

- The daemon runs as **root** via the `tibackupd` systemd service.
- Configuration lives under **`/etc/tibackup/`** (plain INI files); it is created
  on first start. You normally configure everything through the **web UI**, but the
  files can also be edited by hand on headless servers.
- **Web UI access** is gated by a login session. Set/reset the admin password with
  `sudo tiBackup --set-web-password`; the first browser visit also offers a setup
  screen while no password is set.
- By default the UI binds to **`127.0.0.1:8877`** (localhost only). To reach it from
  the LAN, set the bind address, port and TLS certificate/key under the **`[web]`**
  section of `/etc/tibackup/main.conf` — running over the network **without TLS is
  not recommended**, since credentials and LUKS material would travel in clear text.

## Building from source

Requirements (Debian/Ubuntu):

```bash
sudo apt install build-essential cmake qt6-base-dev \
    qt6-httpserver-dev qt6-websockets-dev \
    libpoco-dev libudev-dev libblkid-dev uuid-dev rsync cryptsetup
```

Build `tiBackupLib` first (or check it out next to this repository so it is built
automatically), then:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

The web UI is served by Qt's first-party **`QHttpServer`** + **`QWebSockets`**; the
static frontend assets ship under `var/www/` and are installed to
`/var/lib/tibackup/www`. There is no build step or Node toolchain for the
frontend — it is plain HTML/CSS/JS (Alpine.js + Open Props, vendored).

## License

Licensed under the **GNU General Public License v3.0 or later** (`GPL-3.0-or-later`).
See [LICENSE](LICENSE).

## Package hosting

Package repository hosting is graciously provided by [Cloudsmith](https://cloudsmith.com).
Cloudsmith is the only fully hosted, cloud-native, universal package management
solution that lets you manage your software supply chain with confidence.
