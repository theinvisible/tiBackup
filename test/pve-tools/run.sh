#!/usr/bin/env bash
# Build/run the throwaway Proxmox-tools container used by the PBS packaging E2E
# (test/e2e-pbs-packaging.sh). It provides the real `vma` (pve-qemu-kvm) and
# `proxmox-backup-client`, which the tiBackup daemon invokes when it restores and
# repackages PBS backups. These are Proxmox-only binaries not installable on a
# plain host, so the E2E reaches them through docker-exec shims on the daemon's
# PATH (see the shims created by e2e-pbs-packaging.sh).
#
# The container runs with --network host (so proxmox-backup-client can reach the
# pbs4 test server published on 127.0.0.1:8007) and -v /tmp:/tmp (so restore/
# packaging paths under /tmp are identical inside and outside the container).
set -euo pipefail

IMG=tibackup-test-pve-tools
NAME=tibackup-test-pve-tools-run
DIR="$(cd "$(dirname "$0")" && pwd)"

case "${1:-}" in
    build) docker build -t "$IMG" "$DIR" ;;
    start)
        docker image inspect "$IMG" >/dev/null 2>&1 || docker build -t "$IMG" "$DIR"
        docker rm -f "$NAME" >/dev/null 2>&1 || true
        docker run -d --name "$NAME" --network host -v /tmp:/tmp "$IMG" >/dev/null
        echo "started '$NAME' (vma + proxmox-backup-client; --network host, /tmp shared)" ;;
    stop)  docker rm -f "$NAME" >/dev/null 2>&1 && echo stopped || echo "not running" ;;
    sh)    docker exec -it "$NAME" bash ;;
    *) echo "usage: $0 {build|start|stop|sh}" >&2; exit 2 ;;
esac
