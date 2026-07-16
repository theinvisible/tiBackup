#!/usr/bin/env python3
"""
E2E regression for the PBS TLS certificate-pinning fix.

Before this fix the PBS client ignored *all* SSL errors, so PBS auth (with the
admin password) and every REST call were MITM-able. Now the client verifies the
presented leaf certificate against a pinned SHA-256 fingerprint and fails closed
when nothing is pinned. "Test connection" captures the fingerprint (trust on
first use) so an admin can pin it.

This script drives the running tiBackup daemon's HTTP API against a throwaway
mock PBS (test/mock-pbs.py) with a self-signed cert, and asserts:

  1. datastores with NO pinned fingerprint  -> rejected (fail closed)
  2. "Test connection"                       -> returns the presented fingerprint
  3. captured fingerprint == the mock cert's actual SHA-256
  4. datastores with the CORRECT fingerprint -> succeed
  5. datastores with a WRONG fingerprint     -> rejected
  6. "Test connection" with a wrong pin      -> ok but verified:false

Preconditions:
  * the daemon is running and reachable at BASE_URL
      (e.g. `sudo test/run-e2e-daemon.sh start`, default http://127.0.0.1:8899)
  * openssl is installed
  * on a fresh config the script performs first-run setup; on an already
    provisioned daemon set TIBACKUP_ADMIN_PW to the admin password.

Env overrides: BASE_URL, TIBACKUP_ADMIN_PW, MOCK_HOST, MOCK_PORT.
Exit code 0 = all assertions passed, 1 = a failure.
"""
import json, os, subprocess, sys, tempfile, time, urllib.error, urllib.request
import http.cookiejar

BASE_URL = os.environ.get("BASE_URL", "http://127.0.0.1:8899")
ADMIN_PW = os.environ.get("TIBACKUP_ADMIN_PW", "e2e-admin-pw")
MOCK_HOST = os.environ.get("MOCK_HOST", "127.0.0.1")
MOCK_PORT = int(os.environ.get("MOCK_PORT", "8107"))
HERE = os.path.dirname(os.path.abspath(__file__))

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

def call(method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(BASE_URL + path, data=data, method=method)
    if data is not None:
        req.add_header("Content-Type", "application/json")
    if _csrf:
        req.add_header("X-CSRF-Token", _csrf)
    try:
        r = _op.open(req, timeout=60)
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

def main():
    tmp = tempfile.mkdtemp(prefix="tib-pbs-tls-")
    cert, key = os.path.join(tmp, "pbs.pem"), os.path.join(tmp, "pbs.key")
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-keyout", key,
                    "-out", cert, "-days", "2", "-nodes", "-subj", "/CN=mock-pbs"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    real_fp = subprocess.check_output(
        ["openssl", "x509", "-noout", "-fingerprint", "-sha256", "-in", cert]
    ).decode().split("=", 1)[1].strip()

    mock = subprocess.Popen(
        [sys.executable, os.path.join(HERE, "mock-pbs.py"), "--cert", cert, "--key", key,
         "--host", MOCK_HOST, "--port", str(MOCK_PORT),
         "--user", "root@pam", "--password", "test-pbs-pw"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    uuid = None
    try:
        time.sleep(1.0)
        # Fail fast if our mock could not start (e.g. the port is already taken by
        # a stale mock) - otherwise the daemon would talk to some other server and
        # the fingerprint assertion would fail confusingly.
        if mock.poll() is not None:
            sys.exit(f"mock-pbs failed to start on {MOCK_HOST}:{MOCK_PORT} "
                     f"(exit {mock.returncode}); is the port already in use?")
        authenticate()

        # create a PBS server with NO fingerprint yet
        st, txt = call("POST", "/api/pbs", {
            "uuid": "", "name": "e2e-tls-mock", "host": MOCK_HOST, "port": MOCK_PORT,
            "username": "root@pam", "password": "test-pbs-pw",
            "fingerprint": "", "keyfile": "", "keypass": ""})
        uuid = json.loads(txt)["uuid"]

        # 1) no pin -> reject
        st, _ = call("GET", f"/api/pbs/{uuid}/datastores")
        check(st == 502, "datastores rejected when no fingerprint is pinned (502)")

        # 2/3) test connection captures the presented fingerprint
        st, txt = call("POST", "/api/pbs/test", {"uuid": uuid})
        got_fp = json.loads(txt).get("fingerprint", "")
        check(bool(got_fp), "test connection returns a fingerprint")
        check(_norm(got_fp) == _norm(real_fp),
              "captured fingerprint matches the mock certificate")

        # 4) pin the correct fingerprint -> datastores succeed
        call("PUT", f"/api/pbs/{uuid}", {
            "uuid": uuid, "name": "e2e-tls-mock", "host": MOCK_HOST, "port": MOCK_PORT,
            "username": "root@pam", "password": "", "fingerprint": got_fp,
            "keyfile": "", "keypass": ""})
        st, txt = call("GET", f"/api/pbs/{uuid}/datastores")
        check(st == 200 and "backup-ds" in txt, "datastores succeed with the correct pin (200)")

        # 5) pin a wrong fingerprint -> reject
        wrong = "AA:" + got_fp[3:]
        call("PUT", f"/api/pbs/{uuid}", {
            "uuid": uuid, "name": "e2e-tls-mock", "host": MOCK_HOST, "port": MOCK_PORT,
            "username": "root@pam", "password": "", "fingerprint": wrong,
            "keyfile": "", "keypass": ""})
        st, _ = call("GET", f"/api/pbs/{uuid}/datastores")
        check(st == 502, "datastores rejected with a wrong pin (502)")

        # 6) test with the wrong pin -> ok but verified:false
        st, txt = call("POST", "/api/pbs/test", {"uuid": uuid})
        check(json.loads(txt).get("verified") is False,
              "test reports verified:false against a wrong pin")
    finally:
        if uuid:
            call("DELETE", f"/api/pbs/{uuid}")
        mock.terminate()
        try:
            mock.wait(timeout=5)
        except subprocess.TimeoutExpired:
            mock.kill()
        subprocess.run(["rm", "-rf", tmp])

    print(f"\n{_passed} passed, {_failed} failed")
    sys.exit(1 if _failed else 0)

def _norm(fp):
    return "".join(c for c in fp if c.isalnum()).upper()

if __name__ == "__main__":
    main()
