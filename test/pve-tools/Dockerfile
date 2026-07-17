# Throwaway Proxmox tools container for tiBackup PBS packaging E2E: provides the
# real `vma` (from pve-qemu-kvm) and `proxmox-backup-client`. Run with
# --network host and -v /tmp:/tmp; the daemon reaches these via docker-exec shims.
FROM debian:13
RUN printf '#!/bin/sh\nexit 101\n' > /usr/sbin/policy-rc.d && chmod +x /usr/sbin/policy-rc.d
RUN apt-get update \
 && apt-get install -y --no-install-recommends curl ca-certificates gnupg \
 && curl -fsSLk https://download.proxmox.com/debian/proxmox-archive-keyring-trixie.gpg \
      -o /etc/apt/trusted.gpg.d/proxmox-archive-keyring-trixie.gpg \
 && echo 'deb http://download.proxmox.com/debian/pve trixie pve-no-subscription' > /etc/apt/sources.list.d/pve.list \
 && echo 'deb http://download.proxmox.com/debian/pbs-client trixie main' > /etc/apt/sources.list.d/pbs-client.list \
 && apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      pve-qemu-kvm proxmox-backup-client zstd tar \
 && rm -rf /var/lib/apt/lists/*
CMD ["sleep", "infinity"]
