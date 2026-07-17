#!/usr/bin/env bash
# Prio-3 real-life verification: build the .debs in a fresh container, install them
# (exercising the postinst log migration), then check log location, systemd unit,
# logrotate/tmpfiles, and the web-hardening behaviour against the running daemon.
# Runs as root inside a debian:13 container; sources are read-only at /src.
set -e
export DEBIAN_FRONTEND=noninteractive
printf '#!/bin/sh\nexit 101\n' > /usr/sbin/policy-rc.d && chmod +x /usr/sbin/policy-rc.d

RC=0
pass(){ echo "  PASS  $1"; }
fail(){ echo "  FAIL  $1"; RC=1; }
chk(){ if eval "$1"; then pass "$2"; else fail "$2"; fi; }

echo "########## 1. install build + check deps ##########"
apt-get update -qq
apt-get install -y --no-install-recommends -qq \
  build-essential cmake debhelper fakeroot devscripts \
  qt6-base-dev qt6-base-dev-tools qt6-httpserver-dev qt6-websockets-dev qt6-tools-dev \
  libpoco-dev libudev-dev libblkid-dev uuid-dev \
  ca-certificates adduser rsync openssh-client openssl \
  logrotate systemd curl python3 >/tmp/apt.log 2>&1 || { echo "APT FAILED"; tail -30 /tmp/apt.log; exit 1; }

echo "########## 2. seed a pre-0.9 install: logs under /etc/tibackup/logs ##########"
# Simulate an older install whose logs live in the /etc config tree; the postinst
# must migrate these to /var/log/tibackup and remove the old dir.
mkdir -p /etc/tibackup/logs/backup_detail /etc/tibackup/logs/somejob
echo "legacy detail run" > /etc/tibackup/logs/backup_detail/2019-01-01_00-00__legacy.log
echo "legacy rsync"      > /etc/tibackup/logs/somejob/2019-01-01_00-00_0.log
echo "legacy main log"   > /etc/tibackup/logs/tibackup.log

echo "########## 3. build + install tiBackupLib .debs ##########"
cp -a /src/tiBackupLib /build-lib
cp -a /src/tiBackup /build-app
( cd /build-lib/tibackuplib-dev && debuild -us -uc -b ) >/tmp/build-libdev.log 2>&1 || { echo "LIBDEV BUILD FAILED"; tail -40 /tmp/build-libdev.log; exit 1; }
( cd /build-lib && debuild -us -uc -b ) >/tmp/build-lib.log 2>&1 || { echo "LIB BUILD FAILED"; tail -40 /tmp/build-lib.log; exit 1; }
apt-get install -y /build-lib/tibackuplib-dev_*.deb /tibackuplib_*.deb >/tmp/inst-lib.log 2>&1 || { echo "LIB INSTALL FAILED"; tail -30 /tmp/inst-lib.log; exit 1; }

echo "  --- shipped .so (SOVERSION) ---"
ls -l /usr/lib/libtiBackupLib.so* 2>/dev/null || true
chk '[ -e /usr/lib/libtiBackupLib.so.0 ]' "versioned soname libtiBackupLib.so.0 installed"

echo "########## 4. build + install tiBackup .deb (runs postinst migration) ##########"
( cd /build-app && debuild -us -uc -b ) >/tmp/build-app.log 2>&1 || { echo "APP BUILD FAILED"; tail -50 /tmp/build-app.log; exit 1; }
apt-get install -y /tibackup_*.deb >/tmp/inst-app.log 2>&1 || { echo "APP INSTALL FAILED"; tail -40 /tmp/inst-app.log; exit 1; }

echo "########## 5. postinst log migration ##########"
chk '[ -d /var/log/tibackup ]'                                              "/var/log/tibackup created"
chk '[ -f /var/log/tibackup/backup_detail/2019-01-01_00-00__legacy.log ]'  "legacy detail log moved to /var/log/tibackup"
chk '[ -f /var/log/tibackup/somejob/2019-01-01_00-00_0.log ]'              "legacy per-job log moved to /var/log/tibackup"
chk '[ -f /var/log/tibackup/tibackup.log ]'                                "legacy tibackup.log moved to /var/log/tibackup"
chk '[ ! -e /etc/tibackup/logs ]'                                          "old /etc/tibackup/logs removed"

echo "########## 6. systemd unit + logrotate + tmpfiles ##########"
UNIT=/usr/lib/systemd/system/tibackupd.service
chk '[ -f "'"$UNIT"'" ]' "systemd unit installed"
if command -v systemd-analyze >/dev/null; then
  # Ignore dependency-resolution noise from the minimal container (it has no
  # systemd-udevd.service unit file, which the unit Requires); we only care that
  # the [Service] directives themselves are valid.
  OUT=$(systemd-analyze verify "$UNIT" 2>&1 || true)
  REAL=$(printf '%s\n' "$OUT" | grep -viE "systemd-udevd\.service|Failed to create .*/start:" | grep -vE '^[[:space:]]*$' || true)
  if [ -z "$REAL" ]; then pass "systemd-analyze verify (directives valid)"; else fail "systemd-analyze verify"; printf '%s\n' "$REAL" | sed 's/^/    /'; fi
  # Match ACTIVE directives (line start), not the omitted-directives comment block.
  grep -qE "^ProtectSystem=true" "$UNIT" && pass "ProtectSystem present" || fail "ProtectSystem present"
  grep -qE "^[[:space:]]*NoNewPrivileges=" "$UNIT" && fail "NoNewPrivileges must NOT be an active directive (breaks sudo)" || pass "NoNewPrivileges correctly absent (only in the omitted-list comment)"
else
  echo "  (systemd-analyze not available - skipping verify)"
fi
logrotate -d /etc/logrotate.d/tibackup >/tmp/lr.log 2>&1 && grep -q copytruncate /etc/logrotate.d/tibackup \
  && pass "logrotate config parses + copytruncate" || { fail "logrotate config"; sed 's/^/    /' /tmp/lr.log; }
if command -v systemd-tmpfiles >/dev/null; then
  systemd-tmpfiles --dry-run --clean /usr/lib/tmpfiles.d/tibackup.conf >/tmp/tf.log 2>&1 \
    && pass "tmpfiles config parses" || { fail "tmpfiles config"; sed 's/^/    /' /tmp/tf.log; }
fi

echo "########## 7. start the installed daemon ##########"
export TIBACKUP_WEB_PORT=8899
TIBACKUP_WEB_PORT=8899 TIBACKUP_WEB_DOCROOT=/var/lib/tibackup/www /usr/bin/tiBackup >/tmp/daemon.log 2>&1 &
DPID=$!
for i in $(seq 1 30); do
  curl -sk -o /dev/null "https://127.0.0.1:8899/api/auth/status" 2>/dev/null && break
  kill -0 $DPID 2>/dev/null || { echo "daemon died:"; cat /tmp/daemon.log; exit 1; }
  sleep 1
done
chk '[ -f /var/log/tibackup/tibackup.log ]' "daemon writes tibackup.log under /var/log/tibackup"

echo "########## 8. web hardening: SMTP write-only, From, CSP header, cookie ##########"
python3 /test/api-test.py || RC=1

echo "  --- raw headers (CSP + Set-Cookie) ---"
HDRS=$(curl -skD - -o /dev/null "https://127.0.0.1:8899/")
echo "$HDRS" | grep -i "content-security-policy" | sed 's/^/    /' || true
echo "$HDRS" | grep -iq "content-security-policy:.*frame-ancestors 'none'" \
  && pass "CSP is a real HTTP header with frame-ancestors 'none'" || fail "CSP HTTP header"

# Session cookie Max-Age must be coupled to web/session_ttl (default 3600 here).
LOGIN_HDRS=$(curl -skD - -o /dev/null -X POST -H 'Content-Type: application/json' \
  -d '{"password":"e2e-admin-pw"}' "https://127.0.0.1:8899/api/auth/login")
echo "$LOGIN_HDRS" | grep -i "set-cookie" | sed 's/^/    /' || true
echo "$LOGIN_HDRS" | grep -iq "set-cookie:.*tibackup_sid=.*max-age=3600" \
  && pass "session cookie Max-Age coupled to session_ttl (3600)" || fail "cookie Max-Age"

echo "########## 9. config-pointer migration on daemon restart ##########"
kill $DPID 2>/dev/null || true; sleep 1
# Simulate a config that still points logs at the legacy /etc location.
sed -i 's#^logs=/var/log/tibackup#logs=/etc/tibackup/logs#' /etc/tibackup/main.conf
echo "  before: $(sed -n 's/^logs=//p' /etc/tibackup/main.conf | head -1)"
TIBACKUP_WEB_PORT=8899 TIBACKUP_WEB_DOCROOT=/var/lib/tibackup/www /usr/bin/tiBackup >/tmp/daemon2.log 2>&1 &
DPID2=$!
for i in $(seq 1 30); do curl -sk -o /dev/null "https://127.0.0.1:8899/api/auth/status" 2>/dev/null && break; sleep 1; done
AFTER=$(sed -n 's/^logs=//p' /etc/tibackup/main.conf | head -1)
echo "  after:  $AFTER"
chk '[ "'"$AFTER"'" = "/var/log/tibackup" ]' "paths/logs migrated off /etc on daemon start"
kill $DPID2 2>/dev/null || true

if [ $RC -ne 0 ]; then echo "--- daemon.log tail ---"; tail -20 /tmp/daemon.log; fi
echo "########## RESULT: $([ $RC -eq 0 ] && echo PASS || echo FAIL) ##########"
exit $RC
