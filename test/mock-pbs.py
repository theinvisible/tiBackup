#!/usr/bin/env python3
"""
Minimal mock of the Proxmox Backup Server REST API, just enough to exercise
tiBackup's PBS flow end-to-end (add server -> test connection -> list datastores
-> list backup groups) WITHOUT a real PBS.

tiBackup's pbsClient (tiBackupLib/pbsclient.cpp) only needs:
  POST /api2/json/access/ticket                       -> ticket + CSRFPreventionToken
  GET  /api2/json/admin/datastore                     -> [{store, ...}]
  GET  /api2/json/admin/datastore/<ds>/groups         -> [{backup-type, backup-id, ...}]
and it ignores TLS errors, so a self-signed cert is fine.

Auth model (matches real PBS closely enough for the flow): the ticket endpoint
returns 200 only for the expected username+password; everything else 401. This
lets the UI demonstrate BOTH a successful and a failed "Test connection".

Usage: mock-pbs.py --cert CERT --key KEY [--host 127.0.0.1] [--port 8007]
       [--user root@pam] [--password test-pbs-pw]
"""
import argparse, json, ssl
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

ARGS = None

# A couple of fake datastores and groups so the UI has something to render.
DATASTORES = [
    {"store": "backup-ds",  "comment": "tiBackup E2E mock datastore"},
    {"store": "archive-ds", "comment": "second datastore"},
]
GROUPS = {
    "backup-ds": [
        {"backup-type": "host", "backup-id": "e2e-host",
         "last-backup": 1750000000, "backup-count": 3,
         "files": ["catalog.pcat1.didx", "root.pxar.didx", "index.json.blob"]},
        {"backup-type": "vm", "backup-id": "101",
         "last-backup": 1750086400, "backup-count": 7, "files": []},
    ],
    "archive-ds": [],
}


class Handler(BaseHTTPRequestHandler):
    def _send(self, code, payload):
        body = json.dumps(payload).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *a):  # one tidy line per request, to stderr
        print("mock-pbs %s - %s" % (self.address_string(), fmt % a), flush=True)

    def do_POST(self):
        path = urlparse(self.path).path
        if path == "/api2/json/access/ticket":
            n = int(self.headers.get("Content-Length", 0))
            form = parse_qs(self.rfile.read(n).decode())
            user = form.get("username", [""])[0]
            pw = form.get("password", [""])[0]
            if user == ARGS.user and pw == ARGS.password:
                self._send(200, {"data": {
                    "ticket": "PBS:%s:deadbeef" % user,
                    "CSRFPreventionToken": "0000:cafe",
                    "username": user,
                }})
            else:
                self._send(401, {"data": None,
                                 "errors": {"_": "authentication failure"}})
            return
        self._send(404, {"data": None})

    def do_GET(self):
        parts = urlparse(self.path).path.strip("/").split("/")
        # /api2/json/admin/datastore
        if parts == ["api2", "json", "admin", "datastore"]:
            self._send(200, {"data": DATASTORES})
            return
        # /api2/json/admin/datastore/<ds>/groups
        if (len(parts) == 6 and parts[:4] == ["api2", "json", "admin", "datastore"]
                and parts[5] == "groups"):
            ds = parts[4]
            self._send(200, {"data": GROUPS.get(ds, [])})
            return
        self._send(404, {"data": None})


def main():
    global ARGS
    p = argparse.ArgumentParser()
    p.add_argument("--cert", required=True)
    p.add_argument("--key", required=True)
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=8007)
    p.add_argument("--user", default="root@pam")
    p.add_argument("--password", default="test-pbs-pw")
    ARGS = p.parse_args()

    httpd = ThreadingHTTPServer((ARGS.host, ARGS.port), Handler)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(certfile=ARGS.cert, keyfile=ARGS.key)
    httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)
    print("mock-pbs listening on https://%s:%d (user=%s)" %
          (ARGS.host, ARGS.port, ARGS.user), flush=True)
    httpd.serve_forever()


if __name__ == "__main__":
    main()
