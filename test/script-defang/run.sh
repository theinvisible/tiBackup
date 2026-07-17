#!/usr/bin/env bash
# Real-life E2E for the script-editor defang (pre/post scripts run as a non-root user).
#
# Builds the tiBackup .deb in a fresh Debian 13 container, installs it - which
# exercises the postinst that must create the unprivileged "tibackup" user - then runs
# a real backup job (loop-backed ext4 target) whose pre-backup script must execute as
# that non-root user. Also asserts job-script confinement to the scripts directory and
# that paths/scripts cannot be changed via the web API.
#
# Everything runs inside a throwaway --privileged container; the host is untouched.
# Requires: docker usable by $USER, and a tiBackupLib checkout next to this repo.
#
# Usage:  test/script-defang/run.sh
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$DIR/../.." && pwd)"        # .../tiBackup
SRCROOT="$(cd "$REPO/.." && pwd)"       # parent dir holding tiBackup + tiBackupLib
[ -d "$SRCROOT/tiBackupLib" ] || { echo "tiBackupLib not found next to tiBackup (looked in $SRCROOT)"; exit 1; }
command -v docker >/dev/null || { echo "docker not available"; exit 1; }

# --privileged: the test creates a loop device + mounts ext4 inside the container.
exec docker run --rm --privileged \
  -v "$SRCROOT":/src:ro \
  -v "$DIR":/test:ro \
  debian:13 bash /test/incontainer.sh
