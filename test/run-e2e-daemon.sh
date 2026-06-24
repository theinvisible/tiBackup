#!/usr/bin/env bash
#
# tiBackup E2E test-daemon launcher (runs the daemon as root).
#
# Why this exists: the real mount/umount/backup code path calls mount(8) /
# cryptsetup(8) directly as its own uid, so exercising it end-to-end requires the
# daemon process itself to be root. To keep that root surface tiny, this script
# is the ONLY thing granted via sudoers (NOPASSWD), and it takes NO caller-
# supplied paths or binary names - every path/port is baked in below. A sudoers
# entry on this script therefore cannot be turned into "run an arbitrary program
# as root".
#
# It runs the locally-built binary against a THROWAWAY config tree in /tmp
# (TIBACKUP_CONF override), so the real root-owned /etc/tibackup - and the
# production web password / PBS+SMTP secrets in it - are never touched.
#
# Usage:  sudo test/run-e2e-daemon.sh {start|stop|restart|status|logs}
#
# NOTE: this launches code you (re)compile as root. That trust is inherent to
# testing a root-only mount path; remove the sudoers entry when you are done.

set -euo pipefail

REPO="/home/rene/DEV/tiBackup"
BIN="$REPO/cmake-build-debug/tiBackup"
CONF_DIR="/tmp/tib-e2e"
PIDFILE="$CONF_DIR/daemon.pid"
LOG="$CONF_DIR/daemon.log"

export TIBACKUP_CONF="$CONF_DIR/main.conf"
export TIBACKUP_WEB_PORT="8899"
export TIBACKUP_WEB_DOCROOT="$REPO/var/www"

# If a locally-extracted proxmox-backup-client shim exists (PBS restore E2E),
# put it on the daemon's PATH so it can exec `proxmox-backup-client` by bare name
# without a system-wide install. Harmless when the dir is absent.
[ -d /tmp/tib-e2e-pbsclient/bin ] && export PATH="/tmp/tib-e2e-pbsclient/bin:$PATH"

# Unmount only leftovers under /mnt that point at the test USB stick (/dev/sda*).
# Scoped on purpose: never touches mounts outside /mnt or off other devices.
umount_test_leftovers() {
    findmnt -rno SOURCE,TARGET | awk '$2 ~ "^/mnt/"' | while read -r src tgt; do
        case "$src" in
            /dev/sda*) echo "umount leftover $tgt ($src)"; umount "$tgt" 2>/dev/null || true ;;
        esac
    done
}

running() { [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; }

# Mount the test USB partition READ-ONLY and dump its tree + checksums, so the
# E2E run can prove the backup data really landed on the stick. Read-only on
# purpose: verification must never modify what it inspects.
verify() {
    local mp="/tmp/tib-e2e-verify"
    umount "$mp" 2>/dev/null || true
    mkdir -p "$mp"
    mount -o ro /dev/sda1 "$mp"
    echo "=== tree on /dev/sda1 (type size relpath) ==="
    find "$mp" -mindepth 1 -printf '%y %10s  %P\n' | sort -k3
    echo "=== md5 of files on /dev/sda1 ==="
    find "$mp" -type f -exec md5sum {} \; | sed "s#$mp/##" | sort -k2
    umount "$mp"
    rmdir "$mp" 2>/dev/null || true
}

start() {
    mkdir -p "$CONF_DIR"
    if running; then echo "already running (pid $(cat "$PIDFILE"))"; return 0; fi
    nohup "$BIN" >"$LOG" 2>&1 &
    echo $! >"$PIDFILE"
    echo "started pid $(cat "$PIDFILE") on http://127.0.0.1:${TIBACKUP_WEB_PORT} (log: $LOG)"
}

stop() {
    if running; then kill "$(cat "$PIDFILE")" 2>/dev/null || true; fi
    rm -f "$PIDFILE"
    umount_test_leftovers
    echo "stopped"
}

case "${1:-}" in
    start)   start ;;
    stop)    stop ;;
    restart) stop; start ;;
    status)  if running; then echo "running (pid $(cat "$PIDFILE"))"; else echo "not running"; fi ;;
    verify)  verify ;;
    logs)    echo "--- daemon.log (stdout/stderr) ---"; cat "$LOG" 2>/dev/null || true
             echo "--- tibackup.log (internal) ---";    cat "$CONF_DIR/logs/tibackup.log" 2>/dev/null || true ;;
    detaillog) d="$CONF_DIR/logs/backup_detail"; f="$(ls -t "$d" 2>/dev/null | head -1)"
             echo "--- newest backup_detail: $f ---"; cat "$d/$f" 2>/dev/null || true ;;
    jobconf) for f in "$CONF_DIR"/jobs/*.conf; do echo "--- $f ---"; cat "$f" 2>/dev/null; done ;;
    *)       echo "usage: $0 {start|stop|restart|status|logs}" >&2; exit 2 ;;
esac
