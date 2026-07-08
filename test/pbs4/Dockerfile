# Throwaway Proxmox Backup Server 4 for tiBackup E2E tests. NOT for production.
#
# PBS 4 is based on Debian 13 "trixie" (PBS 3 -> bookworm lives in ../pbs).
# Installs proxmox-backup-server from the Proxmox no-subscription repo. The repo
# is added over HTTP on purpose: in this build environment HTTPS to
# download.proxmox.com is intercepted by a TLS proxy whose cert doesn't match,
# so apt-over-https fails. Packages are GPG-signed, so HTTP + the release key is
# still integrity-checked.
#
# Unlike bookworm, the trixie release-key file is not present on the HTTP mirror
# reachable here (it 404s), only over HTTPS. We therefore fetch the key with
# 'curl -k' (tolerating the intercepting proxy's cert); the fetched key still
# GPG-verifies the HTTP repo, so integrity is no weaker than the bookworm build
# that pulls its key over plain HTTP. Acceptable for a local test server; verify
# the fingerprint if you harden this.
FROM debian:trixie

# Don't let package postinst try to start services via systemd during build
# (there is no init in the build container); the entrypoint starts them instead.
RUN printf '#!/bin/sh\nexit 101\n' > /usr/sbin/policy-rc.d && chmod +x /usr/sbin/policy-rc.d

RUN apt-get update \
 && apt-get install -y --no-install-recommends curl ca-certificates gnupg \
 && curl -fsSLk https://download.proxmox.com/debian/proxmox-archive-keyring-trixie.gpg \
      -o /etc/apt/trusted.gpg.d/proxmox-archive-keyring-trixie.gpg \
 && echo 'deb http://download.proxmox.com/debian/pbs trixie pbs-no-subscription' \
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
