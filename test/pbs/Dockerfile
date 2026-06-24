# Throwaway Proxmox Backup Server for tiBackup E2E tests. NOT for production.
#
# Installs proxmox-backup-server from the Proxmox no-subscription repo. The repo
# is added over HTTP on purpose: in this build environment HTTPS to
# download.proxmox.com is intercepted by a TLS proxy whose cert doesn't match,
# so apt-over-https fails. Packages are GPG-signed, so HTTP + the release key is
# still integrity-checked. The GPG key itself is fetched over HTTP (acceptable
# for a local test server; verify its fingerprint if you harden this).
FROM debian:bookworm

# Don't let package postinst try to start services via systemd during build
# (there is no init in the build container); the entrypoint starts them instead.
RUN printf '#!/bin/sh\nexit 101\n' > /usr/sbin/policy-rc.d && chmod +x /usr/sbin/policy-rc.d

RUN apt-get update \
 && apt-get install -y --no-install-recommends curl ca-certificates gnupg \
 && curl -fsSL http://download.proxmox.com/debian/proxmox-release-bookworm.gpg \
      -o /etc/apt/trusted.gpg.d/proxmox-release-bookworm.gpg \
 && echo 'deb http://download.proxmox.com/debian/pbs bookworm pbs-no-subscription' \
      > /etc/apt/sources.list.d/pbs.list \
 && apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      proxmox-backup-server \
 && rm -rf /var/lib/apt/lists/*

# Default datastore location (override by mounting a volume here to persist).
RUN mkdir -p /datastore

EXPOSE 8007
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
