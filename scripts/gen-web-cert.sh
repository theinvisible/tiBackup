#!/bin/sh
#
# Generate a self-signed TLS certificate for the tiBackup web UI.
#
# Idempotent: without --force it only generates when the cert/key are missing, so
# it is safe to call from the package postinst on every install/upgrade. It is the
# single source of truth for cert generation, shared by debian/tibackup.postinst
# and `tiBackup --regenerate-web-cert`.
#
# The daemon (webserver.cpp) auto-enables HTTPS when <config-dir>/pki/tibackup-web.pem
# and .key exist and no explicit web/tls_cert / web/tls_key are configured.
#
# Usage: gen-web-cert [--force] [<config-dir>]
#   <config-dir> defaults to the directory of $TIBACKUP_CONF, else /etc/tibackup.

set -eu

FORCE=0
CONF_DIR=""
for arg in "$@"; do
    case "$arg" in
        --force) FORCE=1 ;;
        *)       CONF_DIR="$arg" ;;
    esac
done

if [ -z "$CONF_DIR" ]; then
    if [ -n "${TIBACKUP_CONF:-}" ]; then
        CONF_DIR=$(dirname "$TIBACKUP_CONF")
    else
        CONF_DIR="/etc/tibackup"
    fi
fi

PKI_DIR="$CONF_DIR/pki"
CERT="$PKI_DIR/tibackup-web.pem"
KEY="$PKI_DIR/tibackup-web.key"

if [ "$FORCE" -eq 0 ] && [ -f "$CERT" ] && [ -f "$KEY" ]; then
    echo "tiBackup: web certificate already present at $CERT, keeping it."
    exit 0
fi

if ! command -v openssl >/dev/null 2>&1; then
    echo "tiBackup: openssl not found, cannot generate web certificate." >&2
    exit 1
fi

mkdir -p "$PKI_DIR"
chmod 0700 "$PKI_DIR"

HOST=$(hostname 2>/dev/null || echo tibackup)
[ -n "$HOST" ] || HOST=tibackup
FQDN=$(hostname -f 2>/dev/null || echo "$HOST")

# Subject Alternative Names: the machine name, localhost and loopback IPs. Prepend
# the FQDN when it differs from the short hostname.
SAN="DNS:$HOST,DNS:localhost,IP:127.0.0.1,IP:::1"
if [ -n "$FQDN" ] && [ "$FQDN" != "$HOST" ]; then
    SAN="DNS:$FQDN,$SAN"
fi

# umask so the key is never group/world readable even between creation and chmod.
umask 077
openssl req -x509 -newkey rsa:2048 -sha256 -days 3650 -nodes \
    -keyout "$KEY" -out "$CERT" \
    -subj "/CN=$HOST" \
    -addext "subjectAltName=$SAN" >/dev/null 2>&1

chmod 0600 "$KEY"
chmod 0644 "$CERT"

echo "tiBackup: generated self-signed web certificate at $CERT (CN=$HOST, SAN=$SAN)."
