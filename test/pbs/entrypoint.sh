#!/usr/bin/env bash
# Bring up a single-node PBS inside the container (no systemd): start the api +
# proxy daemons, then ensure a datastore exists. root@pam authenticates via PAM
# against the container root password, so we set it from PBS_ROOT_PASSWORD.
set -e

PBS_PW="${PBS_ROOT_PASSWORD:-test-pbs-pw}"
DS_NAME="${PBS_DATASTORE:-backup-ds}"
DS_PATH="/datastore"

echo "root:${PBS_PW}" | chpasswd

mkdir -p /run/proxmox-backup /var/log/proxmox-backup "${DS_PATH}"
# The proxy + datastore I/O run as the unprivileged 'backup' user, so the dirs
# we pre-create as root must be owned by it (the proxy writes temp files into
# /run/proxmox-backup directly). root (the api) can still write to them.
chown backup:backup "${DS_PATH}" /run/proxmox-backup /var/log/proxmox-backup 2>/dev/null || true

# The daemon binaries are not on PATH; they live in the arch lib dir.
PBS_LIBDIR="$(dirname "$(find /usr/lib -name proxmox-backup-proxy 2>/dev/null | head -1)")"
: "${PBS_LIBDIR:=/usr/lib/x86_64-linux-gnu/proxmox-backup}"

echo "Starting proxmox-backup-api..."
"${PBS_LIBDIR}/proxmox-backup-api" &
API_PID=$!

# Wait until the management socket answers (api up).
for i in $(seq 1 60); do
    if proxmox-backup-manager datastore list >/dev/null 2>&1; then break; fi
    if ! kill -0 "$API_PID" 2>/dev/null; then echo "api died early"; wait "$API_PID"; exit 1; fi
    sleep 1
done

# The api generates the ticket-signing keys (authkey.*) and the proxy TLS cert
# on first start. The proxy panics if it starts before they exist (a race the
# datastore-list probe above does not cover), so wait for them explicitly.
echo "Waiting for auth keys + proxy cert..."
for i in $(seq 1 60); do
    [ -f /etc/proxmox-backup/authkey.pub ] && [ -f /etc/proxmox-backup/proxy.pem ] && break
    sleep 1
done

# The proxy refuses to run as root - it must run as the 'backup' user (the
# systemd unit does the same). The api above stays root.
echo "Starting proxmox-backup-proxy (as backup user)..."
runuser -u backup -- "${PBS_LIBDIR}/proxmox-backup-proxy" &
PROXY_PID=$!

# Wait until the public TLS port answers.
for i in $(seq 1 60); do
    if curl -fsSk -m2 https://127.0.0.1:8007/ >/dev/null 2>&1; then break; fi
    sleep 1
done

# Create the test datastore once.
if ! proxmox-backup-manager datastore list 2>/dev/null | grep -qw "${DS_NAME}"; then
    echo "Creating datastore ${DS_NAME} at ${DS_PATH}..."
    proxmox-backup-manager datastore create "${DS_NAME}" "${DS_PATH}"
fi

echo "=================================================================="
echo " PBS test server READY"
echo "   url:        https://127.0.0.1:8007"
echo "   user:       root@pam"
echo "   password:   ${PBS_PW}"
echo "   datastore:  ${DS_NAME}"
echo "   fingerprint:"; proxmox-backup-manager cert info 2>/dev/null | grep -i fingerprint || true
echo "=================================================================="

# Keep the container alive on the proxy; if it exits, the container exits.
wait "$PROXY_PID"
