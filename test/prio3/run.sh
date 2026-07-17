#!/usr/bin/env bash
# Real-life E2E for the Prio-3 hardening batch.
#
# Builds and installs the tiBackup .debs in a fresh Debian 13 container and checks
# the parts that only show up in a real install:
#   - Log migration: logs live under /var/log/tibackup (not /etc); the postinst
#     moves an existing /etc/tibackup/logs tree, and the daemon migrates the
#     paths/logs config key off /etc on the next start.
#   - systemd unit ships and passes `systemd-analyze verify`; logrotate + tmpfiles
#     configs parse.
#   - Web hardening: SMTP password is write-only over the API, the mail From is
#     configurable, the CSP is a real HTTP header, and the session cookie Max-Age
#     is coupled to web/session_ttl.
#
# No backup target / loop device is needed here (that path is covered by
# test/script-defang), so this runs faster. Everything happens inside a throwaway
# container; the host is untouched. Requires docker + a tiBackupLib checkout next
# to this repo.
#
# Usage:  test/prio3/run.sh
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$DIR/../.." && pwd)"        # .../tiBackup
SRCROOT="$(cd "$REPO/.." && pwd)"       # parent dir holding tiBackup + tiBackupLib
[ -d "$SRCROOT/tiBackupLib" ] || { echo "tiBackupLib not found next to tiBackup (looked in $SRCROOT)"; exit 1; }
command -v docker >/dev/null || { echo "docker not available"; exit 1; }

exec docker run --rm \
  -v "$SRCROOT":/src:ro \
  -v "$DIR":/test:ro \
  debian:13 bash /test/incontainer.sh
