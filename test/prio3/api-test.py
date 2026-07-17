#!/usr/bin/env python3
# Prio-3 web-hardening API checks against the installed daemon (run in-container):
#   - logs live under /var/log/tibackup (paths/logs)
#   - the SMTP password is write-only (never returned; only password_set)
#   - the mail From is configurable and "" keeps the stored password
import json, os, ssl, sys, urllib.error, urllib.request, http.cookiejar

PORT = os.environ.get("PORT", "8899")
ctx = ssl.create_default_context(); ctx.check_hostname = False; ctx.verify_mode = ssl.CERT_NONE
cj = http.cookiejar.CookieJar()
op = urllib.request.build_opener(urllib.request.HTTPSHandler(context=ctx),
                                 urllib.request.HTTPCookieProcessor(cj))
BASE = "https://127.0.0.1:%s" % PORT
csrf = None
def call(m, p, body=None):
    d = json.dumps(body).encode() if body is not None else None
    r = urllib.request.Request(BASE + p, data=d, method=m)
    if d is not None: r.add_header("Content-Type", "application/json")
    if csrf: r.add_header("X-CSRF-Token", csrf)
    try:
        x = op.open(r, timeout=30); return x.status, x.read().decode()
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

# --- logs location -----------------------------------------------------------------
prefs = json.loads(call("GET", "/api/prefs")[1])
check(prefs["paths"]["logs"] == "/var/log/tibackup",
      "paths/logs is /var/log/tibackup (got %r)" % prefs["paths"].get("logs"))

# --- SMTP password is write-only ---------------------------------------------------
smtp = prefs.get("smtp", {})
check("password" not in smtp, "GET /api/prefs does NOT return the SMTP password")
check("password_set" in smtp, "GET /api/prefs exposes password_set instead")

# set a password + a custom From
call("PUT", "/api/prefs", {"smtp": {"server": "mail.example.invalid",
                                    "from": "Custom <backup@example.invalid>",
                                    "password": "s3cret-pw"}})
smtp2 = json.loads(call("GET", "/api/prefs")[1])["smtp"]
check("password" not in smtp2, "password still not returned after being set")
check(smtp2.get("password_set") is True, "password_set is true after setting a password")
check(smtp2.get("from") == "Custom <backup@example.invalid>", "mail From is configurable (smtp/from)")

# empty password on PUT must KEEP the existing one (write-only semantics)
call("PUT", "/api/prefs", {"smtp": {"server": "mail.example.invalid", "password": ""}})
smtp3 = json.loads(call("GET", "/api/prefs")[1])["smtp"]
check(smtp3.get("password_set") is True, "empty password on PUT keeps the stored password")

print("\n%d passed, %d failed" % (passed, failed))
sys.exit(1 if failed else 0)
