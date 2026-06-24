#!/usr/bin/env bash
# Build/run the throwaway PBS test server. Published on 127.0.0.1:8007 only.
set -euo pipefail

IMG=tibackup-test-pbs
NAME=tibackup-test-pbs
DIR="$(cd "$(dirname "$0")" && pwd)"
PW="${PBS_ROOT_PASSWORD:-test-pbs-pw}"
DS="${PBS_DATASTORE:-backup-ds}"

case "${1:-}" in
    build) docker build -t "$IMG" "$DIR" ;;
    start)
        docker rm -f "$NAME" >/dev/null 2>&1 || true
        # PBS keeps shared-memory state under /run/proxmox-backup/shmem and
        # refuses to start unless that path is on tmpfs, so mount /run as tmpfs.
        docker run -d --name "$NAME" -p 127.0.0.1:8007:8007 \
            --tmpfs /run:exec --tmpfs /tmp \
            -e PBS_ROOT_PASSWORD="$PW" -e PBS_DATASTORE="$DS" "$IMG"
        echo "started '$NAME' on https://127.0.0.1:8007 (user root@pam / $PW, datastore $DS)"
        echo "follow logs: docker logs -f $NAME" ;;
    stop)  docker rm -f "$NAME" >/dev/null 2>&1 && echo stopped || echo "not running" ;;
    logs)  shift; docker logs "$@" "$NAME" ;;
    sh)    docker exec -it "$NAME" bash ;;
    *) echo "usage: $0 {build|start|stop|logs|sh}" >&2; exit 2 ;;
esac
