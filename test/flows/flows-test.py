#!/usr/bin/env python3
# SSH-server backup + hotplug-job setup against the installed daemon (in-container).
# Run once: does the full SSH flow (create server -> pin host key -> job -> run ->
# assert finished), then creates the hotplug job (the shell replugs its disk and
# verifies the auto-run via the detail log).
import json, os, ssl, sys, time, urllib.error, urllib.request, http.cookiejar

PORT = os.environ.get("PORT", "8899")
UUID1 = os.environ["UUID1"]          # ssh backup target (attached)
UUID2 = os.environ["UUID2"]          # hotplug backup target (detached)
KEY   = os.environ.get("SSH_KEY", "/root/.ssh/tibackup_e2e")

ctx = ssl.create_default_context(); ctx.check_hostname = False; ctx.verify_mode = ssl.CERT_NONE
cj = http.cookiejar.CookieJar()
op = urllib.request.build_opener(urllib.request.HTTPSHandler(context=ctx),
                                 urllib.request.HTTPCookieProcessor(cj))
BASE = "https://127.0.0.1:%s" % PORT
csrf = None
def call(m, p, body=None, timeout=120):
    d = json.dumps(body).encode() if body is not None else None
    r = urllib.request.Request(BASE + p, data=d, method=m)
    if d is not None: r.add_header("Content-Type", "application/json")
    if csrf: r.add_header("X-CSRF-Token", csrf)
    try:
        x = op.open(r, timeout=timeout); return x.status, x.read().decode()
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

# Enable debug logging so the udev observer's diagnostics (Polling / status: add /
# print_device) land in tibackup.log for the hotplug step to inspect.
call("PUT", "/api/prefs", {"debug": True})

# --- SSH server: create, then pin its host key via /api/ssh/test -------------------
st, body = call("POST", "/api/ssh", {"name": "e2e-ssh", "host": "127.0.0.1", "port": 22,
                                     "username": "root", "keyfile": KEY})
check(st == 200, "SSH server created (got %s)" % st)
suuid = json.loads(body)["uuid"]

st, body = call("POST", "/api/ssh/test", {"uuid": suuid})
tr = json.loads(body) if st == 200 else {}
check(st == 200 and tr.get("ok") is True,
      "SSH test connection OK + host key pinned (got %s, %r)" % (st, tr.get("message")))

# --- job that pulls /srv/testdata over SSH onto the loop-ext4 target (UUID1) --------
sshjob = {"name": "e2e-sshjob", "device": "loop", "partition_uuid": UUID1,
          "backupdirs": [], "intervalType": 0, "pbs": False, "save_log": True,
          "ssh": True, "ssh_targets": [{"server_uuid": suuid,
              "backupdirs": [{"source": "/srv/testdata/", "dest": "%MNTBACKUPDIR%/ssh"}]}]}
check(call("POST", "/api/jobs", sshjob)[0] == 200, "SSH job created")
check(call("POST", "/api/jobs/e2e-sshjob/start")[0] == 200, "SSH job started")

status = None
for _ in range(120):
    time.sleep(1)
    s, t = call("GET", "/api/jobs/e2e-sshjob")
    if s == 200:
        status = json.loads(t).get("status")
        if status not in ("running", "standby"): break
print("  ssh job final status:", status)
check(status == "finished", "SSH job finished (mount + rsync-over-ssh ok): %r" % status)

# --- hotplug job: created now, RUN automatically by the shell's re-attach ----------
hpjob = {"name": "e2e-hpjob", "device": "loop", "partition_uuid": UUID2,
         "backupdirs": [{"source": "/src-data/", "dest": "%MNTBACKUPDIR%/hp"}],
         "intervalType": 0, "pbs": False, "ssh": False, "save_log": True,
         "start_backup_on_hotplug": True}
check(call("POST", "/api/jobs", hpjob)[0] == 200, "hotplug job created (start_backup_on_hotplug)")

print("\n%d passed, %d failed" % (passed, failed))
sys.exit(1 if failed else 0)
