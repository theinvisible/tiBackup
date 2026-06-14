/* tiBackup web UI — minimal fetch wrapper.
 * Adds the CSRF header to unsafe requests, parses JSON, and raises a global
 * "unauthorized" event on 401 so the app can drop back to the login view. */
(function () {
  "use strict";

  window.api = {
    csrf: null,

    async req(method, path, body) {
      const opts = { method, headers: {}, credentials: "same-origin" };
      if (body !== undefined) {
        opts.headers["Content-Type"] = "application/json";
        opts.body = JSON.stringify(body);
      }
      if (this.csrf && method !== "GET") {
        opts.headers["X-CSRF-Token"] = this.csrf;
      }

      const res = await fetch(path, opts);

      let data = null;
      const ct = res.headers.get("content-type") || "";
      if (ct.includes("application/json")) {
        try { data = await res.json(); } catch (e) { data = null; }
      }

      if (res.status === 401) {
        window.dispatchEvent(new CustomEvent("unauthorized"));
      }
      if (!res.ok) {
        const msg = (data && data.error) || res.statusText || ("HTTP " + res.status);
        throw Object.assign(new Error(msg), { status: res.status, data });
      }
      return data;
    },

    get(p) { return this.req("GET", p); },
    post(p, b) { return this.req("POST", p, b === undefined ? {} : b); },
    put(p, b) { return this.req("PUT", p, b); },
    del(p) { return this.req("DELETE", p); },
  };
})();
