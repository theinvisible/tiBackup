#!/usr/bin/env bash
#
# E2E for the PBS backup PACKAGING path (Prio-2 Fix B: shell-free vma|zstd and
# tar|zstd, plus pbs_id handling), exercised against the REAL Proxmox tools.
#
# The daemon restores a PBS snapshot with `proxmox-backup-client` and repackages
# it with `vma` (VMs) / `tar`+`zstd` (containers). Those are Proxmox-only binaries
# not installable on a plain host, so we run them inside the pve-tools container
# (test/pve-tools) and expose them to the daemon through tiny docker-exec shims on
# its PATH (the launcher adds /tmp/tib-e2e-pbsclient/bin - see run-e2e-daemon.sh).
#
# It seeds a throwaway PBS (test/pbs4) with a vm/101 and a ct/100 snapshot, runs a
# tiBackup PBS job whose destination folder CONTAINS A SPACE (the exact case the
# old bash-string command construction mangled), then asserts both archives are
# valid and byte-identical to the seed data.
#
# Usage:  test/e2e-pbs-packaging.sh
#   Preconditions: docker (usable by $USER), openssl, python3, and the test USB
#   (/dev/sda1) attached - the daemon must mount a real partition to run a job.
#   The daemon is (re)started for you via `sudo test/run-e2e-daemon.sh` (the only
#   NOPASSWD-sudo entrypoint). Env overrides: BASE_URL, TIBACKUP_ADMIN_PW,
#   PBS_ROOT_PASSWORD, PBS_DATASTORE.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BASE_URL="${BASE_URL:-http://127.0.0.1:8899}"
ADMIN_PW="${TIBACKUP_ADMIN_PW:-e2e-admin-pw}"
PBS_PW="${PBS_ROOT_PASSWORD:-test-pbs-pw}"
DS="${PBS_DATASTORE:-backup-ds}"
DEST="/tmp/tib-e2e-pbs out"          # space on purpose: exercises the shell-free packaging
SHIM_DIR="/tmp/tib-e2e-pbsclient/bin"
SEED="/tmp/tib-e2e-seed"
PVE=tibackup-test-pve-tools-run
say(){ printf '\n=== %s ===\n' "$*"; }

command -v docker  >/dev/null || { echo "need docker";  exit 1; }
command -v openssl >/dev/null || { echo "need openssl"; exit 1; }
command -v python3 >/dev/null || { echo "need python3"; exit 1; }

say "start PBS test server (pbs4) + Proxmox-tools container"
"$REPO/test/pbs4/run.sh" start >/dev/null
"$REPO/test/pve-tools/run.sh" start
for i in $(seq 1 60); do
    c=$(curl -fsSk -o /dev/null -w '%{http_code}' https://127.0.0.1:8007/api2/json/version 2>/dev/null || true)
    { [ "$c" = 401 ] || [ "$c" = 200 ]; } && break
    sleep 1
done

say "install docker-exec shims (vma + proxmox-backup-client)"
mkdir -p "$SHIM_DIR"
cat > "$SHIM_DIR/proxmox-backup-client" <<'SH'
#!/bin/sh
exec docker exec -i -e PBS_REPOSITORY -e PBS_PASSWORD -e PBS_FINGERPRINT \
  -e PBS_ENCRYPTION_PASSWORD -e PROXMOX_OUTPUT_FORMAT \
  tibackup-test-pve-tools-run proxmox-backup-client "$@"
SH
cat > "$SHIM_DIR/vma" <<'SH'
#!/bin/sh
exec docker exec -i tibackup-test-pve-tools-run vma "$@"
SH
chmod 755 "$SHIM_DIR"/proxmox-backup-client "$SHIM_DIR"/vma

say "seed PBS with vm/101 + ct/100"
FP=$(openssl s_client -connect 127.0.0.1:8007 </dev/null 2>/dev/null | openssl x509 -fingerprint -sha256 -noout | sed 's/.*=//')
rm -rf "$SEED"; mkdir -p "$SEED/ctroot/etc" "$SEED/ctroot/data"
head -c 4194304 /dev/urandom > "$SEED/disk.raw"
head -c 65536   /dev/urandom > "$SEED/ctroot/data/blob.bin"
printf 'container root marker\n' > "$SEED/ctroot/etc/marker.txt"
printf 'boot: order=scsi0\ncores: 1\nmemory: 512\nname: e2e-vm\nscsi0: local:101/vm-101-disk-0.raw,size=4M\nscsihw: virtio-scsi-pci\n' > "$SEED/vm.conf"
printf 'arch: amd64\ncores: 1\nhostname: e2e-ct\nmemory: 512\nrootfs: local:100/vm-100-disk-0.raw,size=1G\n' > "$SEED/pct.conf"
DISK_MD5=$(md5sum "$SEED/disk.raw" | cut -d' ' -f1)
BLOB_MD5=$(md5sum "$SEED/ctroot/data/blob.bin" | cut -d' ' -f1)
REPO_SPEC="root@pam@127.0.0.1:8007:$DS"
docker exec -e PBS_PASSWORD="$PBS_PW" -e PBS_FINGERPRINT="$FP" "$PVE" \
    proxmox-backup-client backup qemu-server.conf:"$SEED/vm.conf" drive-scsi0.img:"$SEED/disk.raw" \
    --backup-type vm --backup-id 101 --repository "$REPO_SPEC" >/dev/null
docker exec -e PBS_PASSWORD="$PBS_PW" -e PBS_FINGERPRINT="$FP" "$PVE" \
    proxmox-backup-client backup pct.conf:"$SEED/pct.conf" root.pxar:"$SEED/ctroot" \
    --backup-type ct --backup-id 100 --repository "$REPO_SPEC" >/dev/null

say "restart tiBackup daemon so the shims are on its PATH"
sudo "$REPO/test/run-e2e-daemon.sh" restart
sleep 2

say "configure PBS server + packaging job (spaced dest) and run it"
docker exec "$PVE" rm -rf "$DEST"
FINAL=$(BASE_URL="$BASE_URL" ADMIN_PW="$ADMIN_PW" DS="$DS" DEST="$DEST" python3 - <<'PY'
import json, os, time, urllib.request, urllib.error, http.cookiejar, sys
B=os.environ["BASE_URL"]; cj=http.cookiejar.CookieJar()
op=urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj)); csrf=None
def call(m,p,b=None):
    d=json.dumps(b).encode() if b is not None else None
    r=urllib.request.Request(B+p,data=d,method=m)
    if d is not None: r.add_header("Content-Type","application/json")
    if csrf: r.add_header("X-CSRF-Token",csrf)
    try: x=op.open(r,timeout=180); return x.status,x.read().decode()
    except urllib.error.HTTPError as e: return e.code,e.read().decode()
st=json.loads(call("GET","/api/auth/status")[1])
t=call("POST","/api/setup",{"password":os.environ["ADMIN_PW"]})[1] if st.get("setupRequired") else call("POST","/api/auth/login",{"password":os.environ["ADMIN_PW"]})[1]
csrf=json.loads(t)["csrf"]
# find the test USB partition (/dev/sda1) uuid - the daemon must mount a real partition
devs=json.loads(call("GET","/api/devices")[1]); part=None
for d in devs:
    for p in d.get("partitions",[]):
        if p.get("name")=="/dev/sda1": part=p["uuid"]
if not part: sys.exit("could not find /dev/sda1 in /api/devices (is the test USB attached?)")
uuid=json.loads(call("POST","/api/pbs",{"uuid":"","name":"e2e-pbs-pkg","host":"127.0.0.1","port":8007,
    "username":"root@pam","password":os.environ.get("PBS_PW","test-pbs-pw"),"fingerprint":"","keyfile":"","keypass":""})[1])["uuid"]
fp=json.loads(call("POST","/api/pbs/test",{"uuid":uuid})[1]).get("fingerprint","")
call("PUT","/api/pbs/"+uuid,{"uuid":uuid,"name":"e2e-pbs-pkg","host":"127.0.0.1","port":8007,
    "username":"root@pam","password":"test-pbs-pw","fingerprint":fp,"keyfile":"","keypass":""})
job={"name":"e2e-pbs-pkg","device":"/dev/sda","partition_uuid":part,"backupdirs":[],"intervalType":0,
     "ssh":False,"save_log":True,"pbs":True,"pbs_server_uuid":uuid,"pbs_server_storage":os.environ["DS"],
     "pbs_backup_ids":["vm/101","ct/100"],"pbs_dest_folder":os.environ["DEST"]}
call("DELETE","/api/jobs/e2e-pbs-pkg")
call("POST","/api/jobs",job); call("POST","/api/jobs/e2e-pbs-pkg/start")
status=None
for _ in range(240):
    time.sleep(1)
    s,txt=call("GET","/api/jobs/e2e-pbs-pkg")
    if s==200:
        status=json.loads(txt).get("status")
        if status not in ("running","standby"): break
print(status)
PY
)
echo "job final status: $FINAL"

say "verify packaged archives (validity + byte-fidelity), all in a spaced path"
docker exec -e DISK_MD5="$DISK_MD5" -e BLOB_MD5="$BLOB_MD5" -e DEST="$DEST" "$PVE" sh -c '
set -e; fail=0
VMA=$(ls "$DEST"/101/vzdump-qemu-101-*.vma.zst 2>/dev/null | head -1)
CT=$(ls "$DEST"/100/vzdump-lxc-100-*.tar.zst   2>/dev/null | head -1)
[ -n "$VMA" ] && zstd -t "$VMA" >/dev/null 2>&1 && echo "  PASS  VM archive present & valid zstd" || { echo "  FAIL  VM archive"; fail=1; }
[ -n "$CT" ]  && zstd -t "$CT"  >/dev/null 2>&1 && echo "  PASS  CT archive present & valid zstd" || { echo "  FAIL  CT archive"; fail=1; }
if [ -n "$VMA" ]; then
    rm -rf /tmp/_vv && zstd -dc "$VMA" > /tmp/_v.vma && vma extract /tmp/_v.vma /tmp/_vv >/dev/null 2>&1
    got=$(md5sum /tmp/_vv/disk-drive-scsi0.raw 2>/dev/null | cut -d" " -f1)
    [ "$got" = "$DISK_MD5" ] && echo "  PASS  VM disk byte-identical to source" || { echo "  FAIL  VM disk md5 ($got != $DISK_MD5)"; fail=1; }
fi
if [ -n "$CT" ]; then
    rm -rf /tmp/_ct && mkdir /tmp/_ct && zstd -dc "$CT" | tar -xf - -C /tmp/_ct
    got=$(md5sum /tmp/_ct/data/blob.bin 2>/dev/null | cut -d" " -f1)
    [ "$got" = "$BLOB_MD5" ] && echo "  PASS  CT file byte-identical to source" || { echo "  FAIL  CT file md5 ($got != $BLOB_MD5)"; fail=1; }
    [ -f "/tmp/_ct/etc/vzdump/pct.conf" ] && echo "  PASS  CT config folded into etc/vzdump/" || { echo "  FAIL  CT config missing"; fail=1; }
fi
exit $fail
'
rc=$?
echo
if [ "$FINAL" = "finished" ] && [ $rc -eq 0 ]; then echo "PBS PACKAGING E2E: PASS"; else echo "PBS PACKAGING E2E: FAIL"; exit 1; fi
