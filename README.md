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
> It runs as **root**, listens for udev/hotplug and scheduled events, and performs
> the actual privileged work (mount, rsync, LUKS). It exposes a local **IPC API**
> (for the GUI) and an **HTTP status page**. See [Architecture](#architecture).

## Architecture

tiBackup consists of three parts:

| Component | Role |
|-----------|------|
| **[tiBackupLib](https://github.com/theinvisible/tiBackupLib)** | Shared core library (config, IPC, device handling, backup engine). |
| **[tiBackup](https://github.com/theinvisible/tiBackup)** | Background daemon (`tibackupd`), runs as **root**, performs the actual backups and exposes an IPC + HTTP status API. |
| **[tiBackupUi](https://github.com/theinvisible/tiBackupUi)** | Qt Widgets GUI to configure jobs and settings; runs **unprivileged** and talks to the daemon over IPC. |

All privileged operations live in the daemon. The GUI is an unprivileged client
that asks the daemon to perform config writes, mounts, service control and
backups over the IPC socket — so you never need to run the GUI as root.

## Features

- **Hotplug backups** — connect a disk and the matching job starts automatically
- **Scheduled backups** — daily, weekly or monthly
- **rsync-based** incremental file backups, with optional checksum comparison
- **LUKS encryption** support for backup targets
- **Proxmox Backup Server (PBS)** integration
- **E-mail notifications** when a job finishes (SMTP)
- **Pre/post-backup scripts** with dynamic tiBackup variables
- **HTTP status page** to monitor running/finished backups
- Simple **INI configuration** under `/etc/tibackup` — usable on headless servers

## Installation

Pre-built packages are published via Cloudsmith for **Debian 13 (trixie)**,
**Ubuntu 24.04 (noble)** and **Ubuntu 26.04**. The setup script auto-detects your
distribution:

```bash
curl -1sLf 'https://dl.cloudsmith.io/public/ti-9x5p/tibackup/setup.deb.sh' | sudo -E bash
sudo apt install tibackup
```

This installs the `tibackupd` systemd service. Enable and start it:

```bash
sudo systemctl enable --now tibackupd
```

## Configuration & running

- The daemon runs as **root** via the `tibackupd` systemd service.
- Configuration lives under **`/etc/tibackup/`** (plain INI files); it is created
  on first start. You normally configure everything through the GUI, but the files
  can also be edited by hand on headless servers.
- The daemon listens on a local IPC socket that is restricted to the **`tibackup`**
  group. To let a user drive it from the unprivileged GUI, add them to that group:

  ```bash
  sudo usermod -aG tibackup "$USER"   # then log out and back in
  ```

## Building from source

Requirements (Debian/Ubuntu):

```bash
sudo apt install build-essential cmake qt6-base-dev \
    libpoco-dev libudev-dev libblkid-dev uuid-dev rsync cryptsetup
```

Build `tiBackupLib` first (or check it out next to this repository so it is built
automatically), then:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

The HTTP server is based on the vendored [QtWebApp](http://stefanfrings.de/qtwebapp/)
library, included under `QtWebApp/`.

## License

Licensed under the **GNU General Public License v3.0 or later** (`GPL-3.0-or-later`).
See [LICENSE](LICENSE).

## Package hosting

Package repository hosting is graciously provided by [Cloudsmith](https://cloudsmith.com).
Cloudsmith is the only fully hosted, cloud-native, universal package management
solution that lets you manage your software supply chain with confidence.
