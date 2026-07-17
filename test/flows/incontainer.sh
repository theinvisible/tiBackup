#!/usr/bin/env bash
# SSH-server backup + hotplug regression: build+install the .debs, set up two loop
# ext4 targets + an in-container sshd, then (1) pull a remote dir over rsync-over-SSH
# and (2) auto-run a hotplug job by re-attaching its target disk. Runs as root in a
# --privileged debian:13 container. Sources are read-only at /src.
set -e
export DEBIAN_FRONTEND=noninteractive
printf '#!/bin/sh\nexit 101\n' > /usr/sbin/policy-rc.d && chmod +x /usr/sbin/policy-rc.d
RC=0

echo "########## 1. install build + runtime deps ##########"
apt-get update -qq
apt-get install -y --no-install-recommends -qq \
  build-essential cmake debhelper fakeroot devscripts \
  qt6-base-dev qt6-base-dev-tools qt6-httpserver-dev qt6-websockets-dev qt6-tools-dev \
  libpoco-dev libudev-dev libblkid-dev uuid-dev \
  ca-certificates adduser rsync openssh-client openssh-server openssl \
  cryptsetup util-linux fdisk mount e2fsprogs sudo udev curl python3 >/tmp/apt.log 2>&1 \
  || { echo "APT FAILED"; tail -30 /tmp/apt.log; exit 1; }

echo "########## 2. build + install .debs ##########"
cp -a /src/tiBackupLib /build-lib
cp -a /src/tiBackup /build-app
( cd /build-lib/tibackuplib-dev && debuild -us -uc -b ) >/tmp/build-libdev.log 2>&1 || { echo "LIBDEV FAILED"; tail -40 /tmp/build-libdev.log; exit 1; }
( cd /build-lib && debuild -us -uc -b ) >/tmp/build-lib.log 2>&1 || { echo "LIB FAILED"; tail -40 /tmp/build-lib.log; exit 1; }
apt-get install -y /build-lib/tibackuplib-dev_*.deb /tibackuplib_*.deb >/tmp/inst-lib.log 2>&1 || { echo "LIB INSTALL FAILED"; tail -30 /tmp/inst-lib.log; exit 1; }
( cd /build-app && debuild -us -uc -b ) >/tmp/build-app.log 2>&1 || { echo "APP FAILED"; tail -50 /tmp/build-app.log; exit 1; }
apt-get install -y /tibackup_*.deb >/tmp/inst-app.log 2>&1 || { echo "APP INSTALL FAILED"; tail -40 /tmp/inst-app.log; exit 1; }

echo "########## 3. udevd + two loop-backed ext4 targets ##########"
mkdir -p /run/udev
( /lib/systemd/systemd-udevd --daemon 2>/dev/null || /usr/lib/systemd/systemd-udevd --daemon 2>/dev/null || udevd --daemon 2>/dev/null ) || echo "  (warning: udevd not started)"
# $1=image $2=varname -> creates a dos-partitioned ext4 loop disk, prints "LOOP UUID"
make_loop() {
  local img="$1"
  truncate -s 200M "$img"
  printf 'label: dos\n,,L\n' | sfdisk "$img" >/dev/null 2>&1
  local loop base
  loop=$(losetup -fP --show "$img")
  base=$(basename "$loop")
  udevadm trigger --action=add >/dev/null 2>&1 || true; udevadm settle -t 10 >/dev/null 2>&1 || true
  if [ ! -e "${loop}p1" ] && [ -e "/sys/block/$base/${base}p1/dev" ]; then
    mknod "${loop}p1" b $(sed 's/:/ /' "/sys/block/$base/${base}p1/dev")
  fi
  mkfs.ext4 -F -q "${loop}p1"
  udevadm trigger --action=add >/dev/null 2>&1 || true; udevadm settle -t 10 >/dev/null 2>&1 || true
  echo "$loop $(blkid -s UUID -o value "${loop}p1")"
}
read L1 UUID1 < <(make_loop /disk1.img); echo "  ssh target:     $L1 uuid=$UUID1"
read L2 UUID2 < <(make_loop /disk2.img); echo "  hotplug target: $L2 uuid=$UUID2"
[ -n "$UUID1" ] && [ -n "$UUID2" ] || { echo "FAIL: could not create loop targets"; exit 1; }
# Detach the hotplug target so re-attaching it later triggers the udev add event.
losetup -d "$L2"; udevadm settle -t 10 >/dev/null 2>&1 || true

echo "########## 4. in-container sshd + root key + seed data ##########"
ssh-keygen -A >/dev/null
mkdir -p /root/.ssh && chmod 700 /root/.ssh
# Root-owned key (ssh refuses a key owned by another user; the daemon runs as root).
ssh-keygen -t ed25519 -N '' -f /root/.ssh/tibackup_e2e -C tibackup-e2e >/dev/null
cat /root/.ssh/tibackup_e2e.pub > /root/.ssh/authorized_keys; chmod 600 /root/.ssh/authorized_keys
sed -i 's/^#\?PermitRootLogin.*/PermitRootLogin prohibit-password/' /etc/ssh/sshd_config
sed -i 's/^#\?PubkeyAuthentication.*/PubkeyAuthentication yes/'     /etc/ssh/sshd_config
sed -i 's/^#\?PasswordAuthentication.*/PasswordAuthentication no/'  /etc/ssh/sshd_config
mkdir -p /run/sshd; /usr/sbin/sshd
mkdir -p /srv/testdata/docs && printf 'remote payload\n' > /srv/testdata/docs/remote.txt
mkdir -p /src-data && printf 'hotplug payload\n' > /src-data/hello.txt

echo "########## 5. start the installed daemon ##########"
export TIBACKUP_WEB_PORT=8899
TIBACKUP_WEB_PORT=8899 TIBACKUP_WEB_DOCROOT=/var/lib/tibackup/www /usr/bin/tiBackup >/tmp/daemon.log 2>&1 &
DPID=$!
for i in $(seq 1 30); do
  curl -sk -o /dev/null "https://127.0.0.1:8899/api/auth/status" 2>/dev/null && break
  kill -0 $DPID 2>/dev/null || { echo "daemon died:"; cat /tmp/daemon.log; exit 1; }
  sleep 1
done

echo "########## 6. SSH flow + hotplug-job setup (API) ##########"
UUID1="$UUID1" UUID2="$UUID2" SSH_KEY=/root/.ssh/tibackup_e2e python3 /test/flows-test.py || RC=1

echo "########## 7. hotplug: re-attach the target -> DiskWatcher must auto-run ##########"
HPLOG=/var/log/tibackup/backup_detail
L2b=$(losetup -fP --show /disk2.img); base=$(basename "$L2b")
echo "  re-attached hotplug disk at $L2b (uuid=$UUID2)"
if [ ! -e "${L2b}p1" ] && [ -e "/sys/block/$base/${base}p1/dev" ]; then
  mknod "${L2b}p1" b $(sed 's/:/ /' "/sys/block/$base/${base}p1/dev")
fi
# tiBackupDiskObserver only reacts to USB/ATA disks (it gates on ID_BUS), so a bare
# loop device is ignored - a real USB stick carries ID_BUS=usb. Tag the hotplug
# loop as a USB disk so the SYNTHETIC add event drives the exact same
# onDiskAdded -> mount -> backup handler a real hotplug would (only ID_BUS et al.
# are simulated; the daemon code path is unchanged).
cat > /etc/udev/rules.d/99-tibackup-e2e-loop.rules <<EOF
SUBSYSTEM=="block", KERNEL=="$base", ENV{ID_BUS}="usb", ENV{ID_SERIAL}="tibackup-e2e-hotplug", ENV{ID_VENDOR}="tibackup", ENV{ID_MODEL}="e2e-loop"
EOF
udevadm control --reload-rules 2>/dev/null || udevadm control --reload 2>/dev/null || true
udevadm trigger --action=add --subsystem-match=block --sysname-match="$base" >/dev/null 2>&1 || true
udevadm settle -t 10 >/dev/null 2>&1 || true
echo "  ID_BUS after tag: $(udevadm info -q property -n "$L2b" 2>/dev/null | sed -n 's/^ID_BUS=//p')"
ok=0
for i in $(seq 1 60); do
  f=$(ls -t "$HPLOG"/*__e2e-hpjob.log 2>/dev/null | head -1)
  if [ -n "$f" ] && grep -q "RSYNC Backup successful" "$f" 2>/dev/null; then ok=1; break; fi
  # Re-emit the synthetic add every few seconds: in a container the udev broadcast to
  # the daemon's live monitor can be missed on the first try (a real USB hotplug add
  # is delivered reliably), so retry to keep this repeatable test from flaking.
  if [ $((i % 5)) -eq 0 ]; then udevadm trigger --action=add --subsystem-match=block --sysname-match="$base" >/dev/null 2>&1 || true; fi
  sleep 1
done
if [ "$ok" = "1" ]; then echo "  PASS  hotplug re-attach auto-ran the backup (DiskWatcher OK)"; else echo "  FAIL  hotplug backup did not run within 60s"; RC=1; fi
if [ "$ok" != "1" ]; then
  echo "--- observer diagnostics (tibackup.log) ---"
  grep -iE "Polling devices|status:|Disk added|UDEV|udev_monitor" /var/log/tibackup/tibackup.log 2>/dev/null | tail -40 || echo "  (none)"
  echo "--- udevadm monitor probe (5s while re-triggering) ---"
  (timeout 5 udevadm monitor --udev --subsystem-match=block 2>&1 & sleep 1; udevadm trigger --action=add --subsystem-match=block --sysname-match="$base" >/dev/null 2>&1; wait) | grep -iE "loop|block" | head -20 || true
fi

echo "--- ssh job detail log ---"
sf=$(ls -t "$HPLOG"/*__e2e-sshjob.log 2>/dev/null | head -1); [ -n "$sf" ] && tail -12 "$sf" || echo "  (none)"
echo "--- hotplug job detail log ---"
hf=$(ls -t "$HPLOG"/*__e2e-hpjob.log 2>/dev/null | head -1); [ -n "$hf" ] && tail -12 "$hf" || echo "  (none)"
if [ $RC -ne 0 ]; then echo "--- daemon.log tail ---"; tail -25 /tmp/daemon.log; fi

kill $DPID 2>/dev/null || true
echo "########## RESULT: $([ $RC -eq 0 ] && echo PASS || echo FAIL) ##########"
exit $RC
