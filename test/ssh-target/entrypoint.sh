#!/usr/bin/env bash
# Entrypoint for the throwaway SSH/rsync target. Installs the authorized public
# key, generates host keys, enforces public-key-only auth, and runs sshd.
set -euo pipefail

mkdir -p /root/.ssh
chmod 700 /root/.ssh
if [ -f /authorized_key.pub ]; then
    cat /authorized_key.pub > /root/.ssh/authorized_keys
    chmod 600 /root/.ssh/authorized_keys
    echo "authorized key installed:"
    ssh-keygen -lf /authorized_key.pub || true
else
    echo "WARNING: no /authorized_key.pub mounted; public-key login will fail" >&2
fi

# Host keys (created on first boot; captured/pinned by tiBackup's "Test connection").
ssh-keygen -A

# Public key only; allow root but never by password (test box).
sed -i 's/^#\?PermitRootLogin.*/PermitRootLogin prohibit-password/'         /etc/ssh/sshd_config
sed -i 's/^#\?PubkeyAuthentication.*/PubkeyAuthentication yes/'             /etc/ssh/sshd_config
sed -i 's/^#\?PasswordAuthentication.*/PasswordAuthentication no/'          /etc/ssh/sshd_config

exec /usr/sbin/sshd -D -e
