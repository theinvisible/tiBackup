#!/usr/bin/env python3
# Script-defang API checks against the installed daemon (run inside the container).
import json, os, ssl, sys, time, urllib.error, urllib.request, http.cookiejar

PORT = os.environ.get("PORT", "8899")
PART = os.environ["PART_UUID"]
ctx = ssl.create_default_context(); ctx.check_hostname = False; ctx.verify_mode = ssl.CERT_NONE
cj = http.cookiejar.CookieJar()

BASE = None
for b in ("https://127.0.0.1:%s" % PORT, "http://127.0.0.1:%s" % PORT):
    try:
        op = urllib.request.build_opener(urllib.request.HTTPSHandler(context=ctx),
                                         urllib.request.HTTPCookieProcessor(cj))
        op.open(b + "/api/auth/status", timeout=5)
        BASE = b; break
    except Exception as e:
        last = e
if not BASE:
    sys.exit("daemon not reachable: %r" % last)
print("  base:", BASE)
op = urllib.request.build_opener(urllib.request.HTTPSHandler(context=ctx),
                                 urllib.request.HTTPCookieProcessor(cj))
csrf = None
def call(m, p, body=None):
    d = json.dumps(body).encode() if body is not None else None
    r = urllib.request.Request(BASE + p, data=d, method=m)
    if d is not None: r.add_header("Content-Type", "application/json")
    if csrf: r.add_header("X-CSRF-Token", csrf)
    try:
        x = op.open(r, timeout=90); return x.status, x.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()

passed = failed = 0
def check(c, m):
    global passed, failed
    print(("  PASS  " if c else "  FAIL  ") + m)
    passed += 1 if c else 0; failed += 0 if c else 1

st = json.loads(call("GET", "/api/auth/status")[1])
txt = call("POST", "/api/setup", {"password": "e2e-admin-pw"})[1] if st.get("setupRequired") \
      else call("POST", "/api/auth/login", {"password": "e2e-admin-pw"})[1]
csrf = json.loads(txt)["csrf"]

# --- confinement: a job script outside the scripts dir is rejected at save --------
st, _ = call("POST", "/api/jobs", {"name": "conf-out", "device": "loop", "partition_uuid": PART,
    "backupdirs": [], "intervalType": 0, "pbs": False, "ssh": False,
    "scriptBeforeBackup": "/etc/hostname"})
check(st == 400, "job with script OUTSIDE scripts dir rejected (got %s, want 400)" % st)

# --- prefs lock: paths/scripts cannot be changed via the web API -----------------
before = json.loads(call("GET", "/api/prefs")[1])["paths"]["scripts"]
call("PUT", "/api/prefs", {"paths": {"scripts": "/tmp/evil"}})
after = json.loads(call("GET", "/api/prefs")[1])["paths"]["scripts"]
check(before == after and after != "/tmp/evil",
      "paths/scripts NOT changeable via /api/prefs (stayed %r)" % after)

# --- write a pre-backup script INTO the scripts dir (confined write) --------------
script = "#!/bin/sh\nid -un > /tmp/whoami.out 2>&1\n"
st, _ = call("PUT", "/api/scripts", {"path": before.rstrip("/") + "/whoami.sh", "content": script})
check(st == 200, "PUT script into scripts dir accepted (got %s)" % st)

# --- create + run a real job that uses that script -------------------------------
job = {"name": "defang", "device": "loop", "partition_uuid": PART,
       "backupdirs": [{"source": "/src-data/", "dest": "%MNTBACKUPDIR%/data"}],
       "intervalType": 0, "pbs": False, "ssh": False, "save_log": True,
       "scriptBeforeBackup": before.rstrip("/") + "/whoami.sh"}
st, _ = call("POST", "/api/jobs", job)
check(st == 200, "job with script INSIDE scripts dir accepted (got %s)" % st)
check(call("POST", "/api/jobs/defang/start")[0] == 200, "job started")

status = None
for _ in range(120):
    time.sleep(1)
    s, t = call("GET", "/api/jobs/defang")
    if s == 200:
        status = json.loads(t).get("status")
        if status not in ("running", "standby"): break
print("  job final status:", status)
check(status == "finished", "job finished (mount + rsync + script ok): %r" % status)

print("\n%d passed, %d failed" % (passed, failed))
sys.exit(1 if failed else 0)
