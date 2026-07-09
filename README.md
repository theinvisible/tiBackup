# tiBackup

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg?style=flat-square)](LICENSE)
[![build-check](https://github.com/theinvisible/tiBackup/actions/workflows/build.yml/badge.svg)](https://github.com/theinvisible/tiBackup/actions/workflows/build.yml)

**tiBackup** — an intelligent, disk-based backup system for Linux desktops and
servers, shipping as a background **daemon** (`tibackupd`) with a **built-in,
browser-based web UI** to manage everything (no separate client to install). Plug
in a USB disk and a predefined backup job runs automatically, or schedule jobs
daily/weekly/monthly — with rsync, optional LUKS encryption, pre/post scripts,
e-mail notifications and Proxmox Backup Server integration.

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
- **Proxmox Backup Server (PBS)** integration — compatible with **PBS 3.x and 4.x**
- **E-mail notifications** when a job finishes (SMTP)
- **Pre/post-backup scripts** with dynamic tiBackup variables
- **Built-in web UI** — a modern, browser-based admin interface to manage jobs,
  devices, PBS servers and settings, with **live job status over WebSocket** and a
  native-style file browser; no desktop client required
- **Login-protected** with **HTTPS out of the box** — a self-signed certificate is
  generated on install and the UI is served over TLS on all interfaces (drop in your
  own certificate anytime)
- Simple **INI configuration** under `/etc/tibackup` — usable on headless servers

## Installation

Pre-built `.deb` packages are published to the **iteas APT repository** for
**Debian 13 (trixie)**, **Ubuntu 24.04 (noble)** and **Ubuntu 26.04**. Follow the
repository setup instructions at **[apt.iteas.at](https://apt.iteas.at/)**, then:

```bash
sudo apt update
sudo apt install tibackup
```

The packages are also attached to each
[GitHub release](https://github.com/theinvisible/tiBackup/releases).

This installs the `tibackupd` systemd service and **enables and starts it
automatically**. A **self-signed TLS certificate is generated on install**, so the web
UI comes up over **HTTPS on all interfaces** (port 8877). Set an admin password and
open it in your browser:

```bash
sudo tiBackup --set-web-password          # or use the first-run setup screen
xdg-open https://localhost:8877           # or https://<server-ip>:8877 from the LAN
```

Your browser will warn about the self-signed certificate on first visit — that is
expected; accept it, or drop in your own certificate (see below).

Check the daemon any time with `systemctl status tibackupd`.

## Configuration & running

- The daemon runs as **root** via the `tibackupd` systemd service.
- Configuration lives under **`/etc/tibackup/`** (plain INI files); it is created
  on first start. You normally configure everything through the **web UI**, but the
  files can also be edited by hand on headless servers.
- **Web UI access** is gated by a login session. Set/reset the admin password with
  `sudo tiBackup --set-web-password`; the first browser visit also offers a setup
  screen while no password is set.
- **HTTPS by default.** On install the package generates a self-signed certificate at
  **`/etc/tibackup/pki/tibackup-web.{pem,key}`** and the daemon serves the UI over TLS
  on **all interfaces, port 8877** (`web/bind = 0.0.0.0`). Regenerate it (e.g. after a
  hostname change) with `sudo tiBackup --regenerate-web-cert`, then restart the service.
- **Use your own certificate** by setting `web/tls_cert` and `web/tls_key` (PEM paths)
  in the **`[web]`** section of `/etc/tibackup/main.conf`; these take precedence over the
  generated pair. `web/bind` and `web/port` control the listen address and port.
- Upgrades of existing installs keep their previous `web/bind` (only fresh installs
  default to `0.0.0.0`). Serving the UI over the network **without TLS is not
  recommended**, since credentials and LUKS material would travel in clear text.

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

## Packages

Release `.deb` packages are hosted in the **iteas APT repository**
([apt.iteas.at](https://apt.iteas.at/)) and attached to each
[GitHub release](https://github.com/theinvisible/tiBackup/releases).
