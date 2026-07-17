#!/usr/bin/env python3
"""
E2E regression for the Prio-2 fixes (points 1-3).

Drives the running tiBackup daemon's HTTP API and asserts the three behavioural
changes that have an observable API surface (no PBS/USB hardware required):

  Fix A (non-blocking handlers): a slow PBS/SSH probe used to run synchronously on
    the main event loop, freezing every other request (and the scheduler) for up to
    ~20s. We fire POST /api/ssh/test at a black-holed IP (which hangs in ssh-keyscan)
    and, while it is in flight, assert GET /api/health still answers within ~1s.

  Fix B (pbs_id validation): the jobs POST/PUT handlers now reject PBS backup ids
    that aren't "<vm|ct|host>/<id>" (previously anything was stored, and an id
    without a '/' crashed the daemon at split()[1]). We assert bad ids -> 400 and a
    well-formed id -> accepted.

  Fix C (failed status): a backup that cannot run (here: a job whose partition is
    not attached) must end as status "failed", not "finished". backupStatus::failed
    was declared but never assigned before this fix.

Preconditions:
  * the daemon is running and reachable at BASE_URL
      (e.g. `sudo test/run-e2e-daemon.sh start`, default http://127.0.0.1:8899)
  * on a fresh config the script performs first-run setup; on an already
    provisioned daemon set TIBACKUP_ADMIN_PW to the admin password.

Env overrides: BASE_URL, TIBACKUP_ADMIN_PW, BLACKHOLE_HOST.
Exit code 0 = all assertions passed, 1 = a failure.
"""
import json, os, sys, threading, time, urllib.error, urllib.request
import http.cookiejar

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8899")
ADMIN_PW = os.environ.get("TIBACKUP_ADMIN_PW", "e2e-admin-pw")
# TEST-NET-1 (RFC 5737): guaranteed unroutable, so the ssh probe hangs until timeout.
BLACKHOLE_HOST = os.environ.get("BLACKHOLE_HOST", "192.0.2.1")

_cj = http.cookiejar.CookieJar()
_op = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(_cj))
_csrf = None

_passed = _failed = 0
def check(cond, msg):
    global _passed, _failed
    if cond:
        _passed += 1; print(f"  PASS  {msg}")
    else:
        _failed += 1; print(f"  FAIL  {msg}")

def call(method, path, body=None, opener=None, timeout=60):
    op = opener or _op
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(BASE_URL + path, data=data, method=method)
    if data is not None:
        req.add_header("Content-Type", "application/json")
    if _csrf:
        req.add_header("X-CSRF-Token", _csrf)
    try:
        r = op.open(req, timeout=timeout)
        return r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()

def authenticate():
    global _csrf
    status = json.loads(call("GET", "/api/auth/status")[1])
    if status.get("setupRequired"):
        st, txt = call("POST", "/api/setup", {"password": ADMIN_PW})
        if st != 200:
            sys.exit(f"setup failed: {st} {txt}")
    else:
        st, txt = call("POST", "/api/auth/login", {"password": ADMIN_PW})
        if st != 200:
            sys.exit(f"login failed ({st}); set TIBACKUP_ADMIN_PW to the admin password")
    _csrf = json.loads(txt)["csrf"]

def job_body(name, **over):
    b = {"name": name, "device": "/dev/e2e-none",
         "partition_uuid": "00000000-0000-0000-0000-e2e0badc0de0",
         "backupdirs": [], "intervalType": 0, "pbs": False, "ssh": False}
    b.update(over)
    return b

# --- Fix B: PBS backup-id validation -------------------------------------------
def test_pbs_id_validation():
    print("Fix B: PBS backup-id validation")
    bad = job_body("e2e-p2-badid", pbs=True, pbs_server_uuid="00000000-0000-0000-0000-000000000001",
                   pbs_server_storage="ds", pbs_backup_ids=["vm"], pbs_dest_folder="%MNTBACKUPDIR%/pbs")
    st, _ = call("POST", "/api/jobs", bad)
    check(st == 400, f"POST job with malformed pbs id 'vm' rejected (got {st}, want 400)")

    good = job_body("e2e-p2-goodid", pbs=True, pbs_server_uuid="00000000-0000-0000-0000-000000000001",
                    pbs_server_storage="ds", pbs_backup_ids=["vm/101", "ct/100", "host/srv1"],
                    pbs_dest_folder="%MNTBACKUPDIR%/pbs")
    st, _ = call("POST", "/api/jobs", good)
    check(st == 200, f"POST job with well-formed pbs ids accepted (got {st}, want 200)")

    # PUT the good job with a bad id -> also rejected
    st, _ = call("PUT", "/api/jobs/e2e-p2-goodid",
                 job_body("e2e-p2-goodid", pbs=True, pbs_server_uuid="00000000-0000-0000-0000-000000000001",
                          pbs_server_storage="ds", pbs_backup_ids=["kvm/1"], pbs_dest_folder="%MNTBACKUPDIR%/pbs"))
    check(st == 400, f"PUT job with malformed pbs id 'kvm/1' rejected (got {st}, want 400)")

    call("DELETE", "/api/jobs/e2e-p2-goodid")

# --- Fix C: a backup that cannot run ends as "failed" --------------------------
def test_failed_status():
    print("Fix C: failed status propagation")
    st, _ = call("POST", "/api/jobs", job_body("e2e-p2-fail"))
    if st != 200:
        check(False, f"could not create the failing test job ({st})"); return
    st, _ = call("POST", "/api/jobs/e2e-p2-fail/start")
    check(st == 200, f"start returned ok ({st})")

    status = None
    for _ in range(30):                       # poll up to ~15s for running -> terminal
        time.sleep(0.5)
        st, txt = call("GET", "/api/jobs/e2e-p2-fail")
        if st == 200:
            status = json.loads(txt).get("status")
            if status not in ("running", "standby"):
                break
    check(status == "failed",
          f"job with an unattached partition ends 'failed' (got {status!r})")

    call("DELETE", "/api/jobs/e2e-p2-fail")

# --- Fix A: the main loop stays responsive during a slow SSH probe --------------
def test_non_blocking():
    print("Fix A: handlers do not block the main event loop")
    op2 = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(_cj))  # own connection

    def slow_probe():
        # Hangs in ssh-keyscan/ssh until timeout (~10-30s). Before the fix this
        # froze the single-threaded server; now it runs on a worker thread.
        call("POST", "/api/ssh/test",
             {"name": "blackhole", "host": BLACKHOLE_HOST, "port": 22, "username": "root"},
             opener=op2, timeout=60)

    t = threading.Thread(target=slow_probe, daemon=True)
    t.start()
    time.sleep(1.5)                            # let the probe get in-flight
    check(t.is_alive(), "ssh probe is still in flight (blocking a worker)")

    worst = 0.0
    ok_all = True
    for i in range(5):
        t0 = time.time()
        st, _ = call("GET", "/api/health")
        dt = time.time() - t0
        worst = max(worst, dt)
        if st != 200:
            ok_all = False
    check(ok_all, "GET /api/health kept answering 200 during the probe")
    check(worst < 3.0, f"main loop stayed responsive (worst /api/health latency {worst:.2f}s, want <3s)")
    # don't wait for the ~20s probe to finish; it's a daemon-thread.

def main():
    authenticate()
    test_pbs_id_validation()
    test_failed_status()
    test_non_blocking()
    print(f"\n{_passed} passed, {_failed} failed")
    sys.exit(1 if _failed else 0)

if __name__ == "__main__":
    main()
