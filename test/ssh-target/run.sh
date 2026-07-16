#!/usr/bin/env bash
# Build/run the throwaway SSH/rsync target for rsync-over-SSH E2E tests.
# Published on 127.0.0.1:2222 only.
#
# Auth is public key. By default a dedicated test keypair is generated under
# $SSH_KEYDIR (/tmp/tib-e2e-ssh) and its public half is authorized in the
# container. Configure a tiBackup SSH server with:
#     host 127.0.0.1, port 2222, username root, key file = the private key below
# then "Test connection" to pin the host key, and back up /srv/testdata/*.
#
# NOTE: the tiBackup daemon runs as root, and ssh refuses a private key that is
# owned by another user. If you point the SSH-server key file at the generated
# key, make it root-owned first (e.g. sudo chown root:root "$KEY"), or supply an
# already-root-owned key via SSH_PUBKEY to authorize root's own identity.
set -euo pipefail

IMG=tibackup-test-ssh
NAME=tibackup-test-ssh
DIR="$(cd "$(dirname "$0")" && pwd)"
PORT="${SSH_PORT:-2222}"
KEYDIR="${SSH_KEYDIR:-/tmp/tib-e2e-ssh}"
KEY="$KEYDIR/id_ed25519"
PUB="${SSH_PUBKEY:-$KEY.pub}"

ensure_key() {
    if [ "$PUB" = "$KEY.pub" ] && [ ! -f "$KEY" ]; then
        mkdir -p "$KEYDIR"
        ssh-keygen -t ed25519 -N '' -f "$KEY" -C tibackup-e2e >/dev/null
        echo "generated test keypair: $KEY"
    fi
}

case "${1:-}" in
    build) docker build -t "$IMG" "$DIR" ;;
    start)
        ensure_key
        [ -f "$PUB" ] || { echo "public key not found: $PUB" >&2; exit 1; }
        docker rm -f "$NAME" >/dev/null 2>&1 || true
        # NET_ADMIN lets the 'slow' subcommand shape the link with tc (test box only).
        docker run -d --name "$NAME" --cap-add=NET_ADMIN -p 127.0.0.1:${PORT}:22 \
            -v "$PUB":/authorized_key.pub:ro "$IMG"
        echo "started '$NAME' on ssh://root@127.0.0.1:${PORT} (public-key auth)"
        echo "authorized public key: $PUB"
        [ "$PUB" = "$KEY.pub" ] && echo "private key for the tiBackup SSH server: $KEY"
        echo "seed data at /srv/testdata (docs/, etc/)"
        echo "follow logs: docker logs -f $NAME" ;;
    stop)  docker rm -f "$NAME" >/dev/null 2>&1 && echo stopped || echo "not running" ;;
    logs)  shift; docker logs "$@" "$NAME" ;;
    sh)    docker exec -it "$NAME" bash ;;
    key)   ensure_key; echo "$KEY" ;;
    slow)
        # Simulate a slow, higher-latency link (bandwidth cap + constant delay).
        # NOTE: use a CONSTANT delay and no packet loss - netem jitter/loss over
        # this veth reorders packets and collapses TCP throughput far below the
        # rate cap. Tune with NET_RATE / NET_DELAY.
        RATE="${NET_RATE:-10mbit}"; DELAY="${NET_DELAY:-30ms}"
        docker exec "$NAME" tc qdisc del dev eth0 root 2>/dev/null || true
        docker exec "$NAME" tc qdisc add dev eth0 root handle 1: tbf rate "$RATE" burst 512kb latency 500ms
        docker exec "$NAME" tc qdisc add dev eth0 parent 1: handle 10: netem delay "$DELAY"
        docker exec "$NAME" tc qdisc show dev eth0
        echo "link shaped: rate=$RATE delay=$DELAY (run '$0 normal' to clear)" ;;
    freeze) # drop all egress: simulate a dead connection (tests SSH keepalive abort)
        docker exec "$NAME" tc qdisc del dev eth0 root 2>/dev/null || true
        docker exec "$NAME" tc qdisc add dev eth0 root netem loss 100%
        echo "link frozen (100% loss); run '$0 normal' to restore" ;;
    normal) docker exec "$NAME" tc qdisc del dev eth0 root 2>/dev/null && echo "link restored" || echo "no shaping active" ;;
    *) echo "usage: $0 {build|start|stop|logs|sh|key|slow|freeze|normal}" >&2; exit 2 ;;
esac
