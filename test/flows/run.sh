#!/usr/bin/env bash
# Real-life E2E for two backup-trigger flows, as a regression around the 0.9.0
# changes (scheduler rewrite, DiskWatcher dead-code removal, log move):
#   - SSH-server backup: the daemon pulls a remote dir over rsync-over-SSH onto a
#     loop-backed ext4 target (public-key auth, host key pinned via /api/ssh/test).
#   - Hotplug: a job with start_backup_on_hotplug runs automatically when its
#     target disk is (re)attached - exercises the udev DiskWatcher path end to end.
#
# Both run inside ONE throwaway --privileged debian:13 container (the daemon runs
# as root there, so no host USB / interactive sudo is needed); an sshd in the same
# container is the SSH source. Requires docker + a tiBackupLib checkout next to
# this repo.
#
# Usage:  test/flows/run.sh
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$DIR/../.." && pwd)"
SRCROOT="$(cd "$REPO/.." && pwd)"
[ -d "$SRCROOT/tiBackupLib" ] || { echo "tiBackupLib not found next to tiBackup (looked in $SRCROOT)"; exit 1; }
command -v docker >/dev/null || { echo "docker not available"; exit 1; }

# --privileged: the test creates loop devices, mounts ext4, and needs udevd to
# emit the hotplug uevents the daemon's DiskWatcher listens for.
exec docker run --rm --privileged \
  -v "$SRCROOT":/src:ro \
  -v "$DIR":/test:ro \
  debian:13 bash /test/incontainer.sh
