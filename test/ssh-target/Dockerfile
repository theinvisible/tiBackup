# Throwaway SSH/rsync target for tiBackup rsync-over-SSH E2E tests. NOT for production.
#
# Runs an OpenSSH server (public-key auth only) with rsync installed and a small
# seed data set under /srv/testdata, so a tiBackup job can pull it onto the test
# disk. The public key to authorize is mounted read-only at /authorized_key.pub
# (see run.sh); the entrypoint installs it into root's authorized_keys.
FROM debian:trixie

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      openssh-server rsync ca-certificates iproute2 \
 && rm -rf /var/lib/apt/lists/* \
 && mkdir -p /run/sshd

# Seed data to back up (a couple of dirs with mixed content).
RUN mkdir -p /srv/testdata/docs /srv/testdata/etc \
 && echo "hello from the ssh target" > /srv/testdata/docs/readme.txt \
 && echo "config-value=42"           > /srv/testdata/etc/app.conf \
 && head -c 16384 /dev/urandom       > /srv/testdata/docs/blob.bin

EXPOSE 22
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
