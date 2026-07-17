#!/usr/bin/env bash
# Real-life script-defang verification: build the .deb in a fresh container, install
# it (exercising the postinst that must create the unprivileged "tibackup" user), then
# run a real backup job with a pre-backup script and confirm it executes as that
# non-root user. Also checks job-script confinement + the scripts-dir prefs lock.
# Runs as root inside a --privileged debian:13 container. Sources are read-only at /src.
set -e
export DEBIAN_FRONTEND=noninteractive
printf '#!/bin/sh\nexit 101\n' > /usr/sbin/policy-rc.d && chmod +x /usr/sbin/policy-rc.d

echo "########## 1. install build + runtime deps ##########"
if command -v debuild >/dev/null && command -v cmake >/dev/null && command -v sfdisk >/dev/null; then
  echo "  deps already present (base image) - skipping apt"
else
  apt-get update -qq
  apt-get install -y --no-install-recommends -qq \
    build-essential cmake debhelper fakeroot devscripts \
    qt6-base-dev qt6-base-dev-tools qt6-httpserver-dev qt6-websockets-dev qt6-tools-dev \
    libpoco-dev libudev-dev libblkid-dev uuid-dev \
    ca-certificates adduser rsync openssh-client cryptsetup util-linux fdisk mount e2fsprogs \
    sudo udev curl python3 openssl >/tmp/apt.log 2>&1 || { echo "APT FAILED"; tail -30 /tmp/apt.log; exit 1; }
fi

echo "########## 2. build tiBackupLib .debs ##########"
cp -a /src/tiBackupLib /build-lib
cp -a /src/tiBackup /build-app
( cd /build-lib/tibackuplib-dev && debuild -us -uc -b ) >/tmp/build-libdev.log 2>&1 || { echo "LIBDEV BUILD FAILED"; tail -40 /tmp/build-libdev.log; exit 1; }
( cd /build-lib && debuild -us -uc -b ) >/tmp/build-lib.log 2>&1 || { echo "LIB BUILD FAILED"; tail -40 /tmp/build-lib.log; exit 1; }
apt-get install -y /build-lib/tibackuplib-dev_*.deb /tibackuplib_*.deb >/tmp/inst-lib.log 2>&1 || { echo "LIB INSTALL FAILED"; tail -30 /tmp/inst-lib.log; exit 1; }

echo "########## 3. build + install tiBackup .deb (runs postinst) ##########"
( cd /build-app && debuild -us -uc -b ) >/tmp/build-app.log 2>&1 || { echo "APP BUILD FAILED"; tail -50 /tmp/build-app.log; exit 1; }
apt-get install -y /tibackup_*.deb >/tmp/inst-app.log 2>&1 || { echo "APP INSTALL FAILED"; tail -40 /tmp/inst-app.log; exit 1; }

echo "########## 4. postinst must have created the unprivileged user ##########"
if getent passwd tibackup >/dev/null; then
  echo "  PASS  user 'tibackup' created: $(getent passwd tibackup)"
else
  echo "  FAIL  user 'tibackup' NOT created"; exit 1
fi
echo "  sudoers template: $(ls -l /etc/sudoers.d/tibackup-scripts 2>/dev/null || echo MISSING)"
visudo -cf /etc/sudoers.d/tibackup-scripts && echo "  PASS  sudoers template valid" || echo "  FAIL  sudoers template invalid"

echo "########## 5. loop-backed ext4 partition (the backup target) ##########"
for c in truncate sfdisk losetup mkfs.ext4 blkid udevadm; do command -v "$c" >/dev/null || { echo "MISSING TOOL: $c"; exit 1; }; done
# The daemon enumerates disks via libudev and gates on DEVTYPE=disk + ID_PART_TABLE_TYPE
# (set by udevd's blkid builtin). So udevd must run and process the loop device.
mkdir -p /run/udev
( /lib/systemd/systemd-udevd --daemon 2>/dev/null || /usr/lib/systemd/systemd-udevd --daemon 2>/dev/null || udevd --daemon 2>/dev/null ) || echo "  (warning: could not start udevd)"
truncate -s 200M /disk.img
printf 'label: dos\n,,L\n' | sfdisk /disk.img >/dev/null 2>&1
LOOP=$(losetup -fP --show /disk.img)
base=$(basename "$LOOP")
# Ensure the partition node exists even if udev is slow (create from sysfs major:minor).
udevadm trigger --action=add >/dev/null 2>&1 || true; udevadm settle -t 10 >/dev/null 2>&1 || true
if [ ! -e "${LOOP}p1" ] && [ -e "/sys/block/$base/${base}p1/dev" ]; then
  mknod "${LOOP}p1" b $(sed 's/:/ /' "/sys/block/$base/${base}p1/dev")
fi
[ -e "${LOOP}p1" ] || { echo "FAIL: ${LOOP}p1 not created"; ls -l /dev/loop* /sys/block/$base 2>/dev/null; exit 1; }
mkfs.ext4 -F -q "${LOOP}p1"
udevadm trigger --action=add >/dev/null 2>&1 || true; udevadm settle -t 10 >/dev/null 2>&1 || true
export PART_UUID=$(blkid -s UUID -o value "${LOOP}p1")
echo "  loop=$LOOP uuid=$PART_UUID"
[ -n "$PART_UUID" ] || { echo "FAIL: no UUID on ${LOOP}p1"; exit 1; }
echo "  disk udev props: $(udevadm info -q property -n "$LOOP" 2>/dev/null | grep -E '^(DEVTYPE|ID_PART_TABLE_TYPE)=' | tr '\n' ' ')"
mkdir -p /src-data && printf 'payload\n' > /src-data/hello.txt

echo "########## 6. start the installed daemon ##########"
export TIBACKUP_WEB_PORT=8899
TIBACKUP_WEB_PORT=8899 TIBACKUP_WEB_DOCROOT=/var/lib/tibackup/www /usr/bin/tiBackup >/tmp/daemon.log 2>&1 &
DPID=$!
for i in $(seq 1 30); do
  curl -sk -o /dev/null "https://127.0.0.1:8899/api/auth/status" 2>/dev/null && break
  curl -s  -o /dev/null "http://127.0.0.1:8899/api/auth/status" 2>/dev/null && break
  kill -0 $DPID 2>/dev/null || { echo "daemon died:"; cat /tmp/daemon.log; exit 1; }
  sleep 1
done
echo "  scripts dir on disk: $(ls -ld /etc/tibackup/scripts 2>/dev/null)"

echo "########## 7. API test: confinement, prefs-lock, non-root script exec ##########"
PART_UUID="$PART_UUID" python3 /test/api-test.py
RC=$?

echo "########## 8. proof: which user did the script run as? ##########"
if [ -f /tmp/whoami.out ]; then
  echo "  script wrote user: $(cat /tmp/whoami.out)"
  if [ "$(cat /tmp/whoami.out)" = "tibackup" ]; then echo "  PASS  pre-backup script ran as NON-ROOT (tibackup)"; else echo "  FAIL  script ran as $(cat /tmp/whoami.out), expected tibackup"; RC=1; fi
else
  echo "  FAIL  script produced no output (did it run?)"; RC=1
fi
echo "--- newest backup detail log ---"
d=/etc/tibackup/logs/backup_detail; f=$(ls -t "$d" 2>/dev/null | head -1)
[ -n "$f" ] && tail -25 "$d/$f" || echo "  (no detail log)"
if [ $RC -ne 0 ]; then
  echo "--- daemon.log tail ---"; tail -15 /tmp/daemon.log
  echo "--- tibackup.log (runScriptAsUser diag) ---"
  grep -iE "runScriptAsUser|script" /etc/tibackup/logs/tibackup.log 2>/dev/null | tail -15 || echo "(no internal log)"
  echo "--- ls staged temp + /tmp perms ---"; ls -ld /tmp; ls -l /tmp/tibackup-* 2>/dev/null || echo "(temp already removed)"
fi

kill $DPID 2>/dev/null || true
losetup -d "$LOOP" 2>/dev/null || true
echo "########## RESULT: $([ $RC -eq 0 ] && echo PASS || echo FAIL) ##########"
exit $RC
