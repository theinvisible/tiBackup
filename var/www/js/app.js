/* tiBackup web UI — Alpine application root.
 * Drives the whole admin panel against the daemon's /api + /ws endpoints.
 * No build step: one Alpine component, plain fetch, vendored libs. */

// Default objects so the (hidden) editor modals never bind against null —
// Alpine still evaluates x-show'd bindings, so job/pbsForm must always be objects.
function blankJob() {
  return {
    name: "", device: "", partition_uuid: "", backupdirs: [],
    delete_add_file_on_dest: false, start_backup_on_hotplug: false,
    save_log: true, compare_via_checksum: false,
    notify: false, notifyRecipients: "",
    scriptBeforeBackup: "", scriptAfterBackup: "",
    intervalType: 0, intervalTime: "00:00", intervalDay: 0,
    encLUKSType: 0, encLUKSFilePath: "",
    pbs: false, pbs_server_uuid: "", pbs_server_storage: "", pbs_backup_ids: [], pbs_dest_folder: "",
  };
}
function blankPbs() {
  return { uuid: "", name: "", host: "", port: 8007, username: "", password: "", fingerprint: "", keyfile: "", keypass: "" };
}

// Daemon-log levels, ordered by severity. The daemon writes "[LEVEL]" into every
// line (see logging.cpp); the Log tab filters by it, hiding DEBUG by default.
const LOG_LEVELS = { debug: 0, info: 1, warn: 2, error: 3, crit: 4 };
function logLevelOf(line) {
  const m = String(line).match(/\[(DEBUG|INFO|WARN|ERROR|CRIT)\]/);
  return m ? m[1].toLowerCase() : "info";   // untagged lines stay visible
}

function app() {
  return {
    // ---- view state -------------------------------------------------------
    view: "loading",          // loading | setup | login | app
    page: "dashboard",        // dashboard | pbs | settings | logs
    theme: localStorage.getItem("theme") || "dark",
    busy: false,
    wsLive: false,
    toasts: [],
    aboutOpen: false,

    // auth forms
    loginPw: "", loginErr: "",
    setupPw: "", setupPw2: "", setupErr: "",

    // dashboard data
    health: {}, jobs: [],
    // logs tab: daemon log (level-filtered) + run logs grouped by job
    runs: [], runText: "", runOpen: false,
    logLines: [], logLevel: "info", openGroups: {},
    pollTimer: null,

    // devices (loaded for the job editor)
    devices: [],

    // job editor
    editorOpen: false, editorNew: false, editorTab: "general", editorOrigName: "",
    job: blankJob(), editorDevname: "", editorMountDir: "", partInfo: null,
    folderSource: "", folderDest: "", folderEditIndex: -1,

    // pbs
    pbs: [], pbsEditorOpen: false, pbsForm: blankPbs(), pbsTest: "",
    // pbs data used inside the job editor
    pbsDatastores: [], pbsGroups: [],

    // settings
    prefs: null,

    // script editor
    scriptOpen: false, scriptPath: "", scriptContent: "", scriptField: "",

    // path picker (native-style file browser)
    pickerOpen: false, pickerMode: "src", pickerUuid: "", pickerFiles: false,
    pickerBase: "", pickerPath: "", pickerParent: "", pickerEntries: [],
    pickerResolve: null, pickerSel: "", pickerSort: { key: "name", dir: 1 },
    folderIcon: '<svg viewBox="0 0 24 24" class="fi"><path d="M10 4H4a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-8l-2-2z" fill="#5b8def"/></svg>',
    fileIcon: '<svg viewBox="0 0 24 24" class="fi"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8l-6-6z" fill="#9aa4b2"/><path d="M14 2v6h6z" fill="#ced6e0"/></svg>',

    // -----------------------------------------------------------------------
    async init() {
      document.documentElement.dataset.theme = this.theme;
      window.addEventListener("unauthorized", () => {
        api.csrf = null; this.stopPoll(); window.liveSocket.close(); this.wsLive = false;
        if (this.view === "app") this.view = "login";
      });
      try {
        const s = await api.get("/api/auth/status");
        if (s.setupRequired) this.view = "setup";
        else if (s.authenticated) { api.csrf = s.csrf; await this.enterApp(); }
        else this.view = "login";
      } catch (e) { this.view = "login"; }
    },

    toggleTheme() {
      this.theme = this.theme === "dark" ? "light" : "dark";
      localStorage.setItem("theme", this.theme);
      document.documentElement.dataset.theme = this.theme;
    },

    toast(msg, kind) {
      const id = Date.now() + Math.random();
      this.toasts.push({ id, msg, kind: kind || "ok" });
      setTimeout(() => { this.toasts = this.toasts.filter((t) => t.id !== id); }, 4000);
    },

    // ---- auth -------------------------------------------------------------
    async doSetup() {
      this.setupErr = "";
      if (this.setupPw.length < 8) { this.setupErr = "Password must be at least 8 characters."; return; }
      if (this.setupPw !== this.setupPw2) { this.setupErr = "Passwords do not match."; return; }
      this.busy = true;
      try {
        const r = await api.post("/api/setup", { password: this.setupPw });
        api.csrf = r.csrf; this.setupPw = this.setupPw2 = "";
        await this.enterApp();
      } catch (e) { this.setupErr = e.message; }
      this.busy = false;
    },
    async doLogin() {
      this.loginErr = ""; this.busy = true;
      try {
        const r = await api.post("/api/auth/login", { password: this.loginPw });
        api.csrf = r.csrf; this.loginPw = "";
        await this.enterApp();
      } catch (e) { this.loginErr = e.message || "Sign in failed"; }
      this.busy = false;
    },
    async doLogout() {
      try { await api.post("/api/auth/logout"); } catch (e) {}
      api.csrf = null; this.stopPoll(); window.liveSocket.close(); this.wsLive = false;
      this.view = "login";
    },

    // ---- navigation -------------------------------------------------------
    async enterApp() {
      this.view = "app";
      this.connectLive();
      this.go("dashboard");
    },
    go(p) {
      this.page = p;
      if (p === "dashboard") this.loadDashboard();
      else if (p === "pbs") this.loadPbs();
      else if (p === "settings") this.loadPrefs();
      else if (p === "logs") this.loadLogs();
    },

    // ---- live channel (WebSocket, polling fallback) -----------------------
    connectLive() {
      window.liveSocket.connect(
        (msg) => this.onLiveMessage(msg),
        () => { this.wsLive = true; this.stopPoll(); },
        () => { this.wsLive = false; this.startPoll(); }
      );
    },
    onLiveMessage(msg) {
      if (msg.type === "jobStatus") {
        const j = this.jobs.find((x) => x.name === msg.name);
        if (j) j.status = msg.status;
      } else if (msg.type === "logTail") {
        this.pushLog(msg.line);
        if (this.page === "logs") this.scrollLog();
      }
    },
    startPoll() {
      this.stopPoll();
      if (this.view !== "app") return;
      this.pollTimer = setInterval(() => { if (this.page === "dashboard") this.loadDashboard(true); }, 5000);
    },
    stopPoll() { if (this.pollTimer) { clearInterval(this.pollTimer); this.pollTimer = null; } },

    // ---- dashboard --------------------------------------------------------
    async loadDashboard(silent) {
      try {
        const [h, j] = await Promise.all([
          api.get("/api/health"), api.get("/api/jobs"),
        ]);
        this.health = h; this.jobs = j;
      } catch (e) { if (!silent) console.error(e); }
    },
    scrollLog() {
      this.$nextTick(() => { const el = this.$refs.logbox; if (el) el.scrollTop = el.scrollHeight; });
    },
    get runningCount() { return this.jobs.filter((j) => j.status === "running").length; },
    statusClass(s) { return "badge badge-" + s; },

    async startJob(name) {
      try { await api.post("/api/jobs/" + encodeURIComponent(name) + "/start"); this.toast("Backup started: " + name); }
      catch (e) { this.toast("Could not start: " + e.message, "error"); }
    },
    async deleteJob(name) {
      if (!confirm("Delete backup job \"" + name + "\"?")) return;
      try { await api.del("/api/jobs/" + encodeURIComponent(name)); this.toast("Job deleted"); this.loadDashboard(); }
      catch (e) { this.toast("Delete failed: " + e.message, "error"); }
    },

    // ---- job editor -------------------------------------------------------
    emptyJob() { return blankJob(); },
    async newJob() {
      this.job = this.emptyJob(); this.editorNew = true; this.editorTab = "general"; this.editorOrigName = "";
      this.editorDevname = ""; this.editorMountDir = ""; this.partInfo = null; this.resetFolderForm();
      this.editorOpen = true;
      await Promise.all([this.loadDevices(), this.loadPbs()]);
    },
    async editJob(name) {
      try {
        const job = await api.get("/api/jobs/" + encodeURIComponent(name));
        if (!job.backupdirs) job.backupdirs = [];
        const wantUuid = job.partition_uuid;
        this.editorNew = false; this.editorTab = "general"; this.editorOrigName = name;
        this.editorMountDir = ""; this.partInfo = null; this.resetFolderForm();
        // Devices must be loaded before binding the partition: the partition
        // <select> only shows the stored UUID if its <option> already exists.
        await Promise.all([this.loadDevices(), this.loadPbs()]);
        const d = this.devices.find((x) => x.partitions.some((p) => p.uuid === wantUuid));
        // Bind the job with the partition cleared and set the device first so its
        // <option>s render; then (next tick, once they exist) apply the UUID so
        // x-model can select it. Without this two-step, a cold first-open binds the
        // value before the options exist and the select stays blank until reopened.
        job.partition_uuid = "";
        this.job = job;
        this.editorDevname = d ? d.devname : "";
        this.editorOpen = true;
        await this.$nextTick();
        this.job.partition_uuid = wantUuid;
        this.refreshPartInfo();
      } catch (e) { this.toast("Could not load job: " + e.message, "error"); }
    },
    async loadDevices() {
      try { this.devices = await api.get("/api/devices"); } catch (e) { this.devices = []; }
    },
    get editorPartitions() {
      const d = this.devices.find((x) => x.devname === this.editorDevname);
      return d ? d.partitions : [];
    },
    get selectedPartType() {
      const p = this.editorPartitions.find((x) => x.uuid === this.job.partition_uuid);
      if (p) return p.type;
      return this.partInfo && this.partInfo.type ? this.partInfo.type : "";
    },
    onDeviceChange() {
      this.job.partition_uuid = ""; this.editorMountDir = ""; this.partInfo = null;
      const d = this.devices.find((x) => x.devname === this.editorDevname);
      this.job.device = d ? (d.vendor + " " + d.model + " (" + d.devname + ")").trim() : "";
    },
    onPartitionChange() { this.editorMountDir = ""; this.refreshPartInfo(); },
    async refreshPartInfo() {
      if (!this.job.partition_uuid) { this.partInfo = null; return; }
      try {
        this.partInfo = await api.get("/api/devices/partition/" + encodeURIComponent(this.job.partition_uuid));
        if (this.partInfo.mounted) this.editorMountDir = this.partInfo.mountDir;
      } catch (e) { this.partInfo = null; }
    },
    async mountForBrowse() {
      if (!this.job.partition_uuid) { this.toast("Select a partition first", "error"); return; }
      try {
        const r = await api.post("/api/devices/partition/" + encodeURIComponent(this.job.partition_uuid) + "/mount",
          { luksType: this.job.encLUKSType, luksFilePath: this.job.encLUKSFilePath });
        this.editorMountDir = r.mountDir; this.toast("Mounted at " + r.mountDir); this.refreshPartInfo();
      } catch (e) { this.toast("Mount failed: " + e.message, "error"); }
    },
    get folderEditing() { return this.folderEditIndex >= 0; },
    addFolder() {
      const src = this.folderSource.trim(), dst = this.folderDest.trim();
      if (!src || !dst) { this.toast("Source and destination required", "error"); return; }
      if (this.folderEditIndex >= 0) {
        this.job.backupdirs.splice(this.folderEditIndex, 1, { source: src, dest: dst });
      } else {
        this.job.backupdirs.push({ source: src, dest: dst });
      }
      this.resetFolderForm();
    },
    editFolder(i) {
      const f = this.job.backupdirs[i];
      this.folderSource = f.source; this.folderDest = f.dest; this.folderEditIndex = i;
    },
    cancelFolderEdit() { this.resetFolderForm(); },
    resetFolderForm() { this.folderSource = ""; this.folderDest = ""; this.folderEditIndex = -1; },
    removeFolder(i) {
      this.job.backupdirs.splice(i, 1);
      if (this.folderEditIndex === i) this.resetFolderForm();
      else if (this.folderEditIndex > i) this.folderEditIndex--;
    },

    async saveJob() {
      if (!this.job.name) { this.toast("Job name required", "error"); return; }
      this.busy = true;
      try {
        if (this.editorNew) await api.post("/api/jobs", this.job);
        else await api.put("/api/jobs/" + encodeURIComponent(this.editorOrigName || this.job.name), this.job);
        this.editorOpen = false; this.toast("Job saved");
        this.loadDashboard();
      } catch (e) { this.toast("Save failed: " + e.message, "error"); }
      this.busy = false;
    },

    // ---- PBS --------------------------------------------------------------
    async loadPbs() { try { this.pbs = await api.get("/api/pbs"); } catch (e) { this.pbs = []; } },
    newPbs() { this.pbsForm = blankPbs(); this.pbsTest = ""; this.pbsEditorOpen = true; },
    async editPbs(uuid) {
      try { const s = await api.get("/api/pbs/" + encodeURIComponent(uuid)); s.password = ""; s.keypass = ""; this.pbsForm = s; this.pbsTest = ""; this.pbsEditorOpen = true; }
      catch (e) { this.toast(e.message, "error"); }
    },
    async savePbs() {
      if (!this.pbsForm.name || !this.pbsForm.host) { this.toast("Name and host required", "error"); return; }
      this.busy = true;
      try {
        if (this.pbsForm.uuid) await api.put("/api/pbs/" + encodeURIComponent(this.pbsForm.uuid), this.pbsForm);
        else await api.post("/api/pbs", this.pbsForm);
        this.pbsEditorOpen = false; this.toast("PBS server saved"); this.loadPbs();
      } catch (e) { this.toast("Save failed: " + e.message, "error"); }
      this.busy = false;
    },
    async deletePbs(uuid) {
      if (!confirm("Remove this PBS server?")) return;
      try { await api.del("/api/pbs/" + encodeURIComponent(uuid)); this.toast("PBS server removed"); this.loadPbs(); }
      catch (e) { this.toast("Delete failed: " + e.message, "error"); }
    },
    async testPbs() {
      this.pbsTest = "…";
      try { const r = await api.post("/api/pbs/test", this.pbsForm.uuid ? { uuid: this.pbsForm.uuid } : this.pbsForm); this.pbsTest = r.ok ? "Connection OK (" + r.status + ")" : "Failed (" + r.status + ")"; }
      catch (e) { this.pbsTest = "Failed: " + e.message; }
    },
    // job-editor PBS helpers
    async loadDatastores() {
      this.pbsDatastores = []; this.pbsGroups = [];
      if (!this.job.pbs_server_uuid) return;
      try { this.pbsDatastores = await api.get("/api/pbs/" + encodeURIComponent(this.job.pbs_server_uuid) + "/datastores"); }
      catch (e) { this.toast("PBS: " + e.message, "error"); }
    },
    async loadGroups() {
      this.pbsGroups = [];
      if (!this.job.pbs_server_uuid || !this.job.pbs_server_storage) return;
      try { this.pbsGroups = await api.get("/api/pbs/" + encodeURIComponent(this.job.pbs_server_uuid) + "/datastores/" + encodeURIComponent(this.job.pbs_server_storage) + "/groups"); }
      catch (e) { this.toast("PBS: " + e.message, "error"); }
    },
    groupId(g) { return (g["backup-type"] || g.backuptype || "") + "/" + (g["backup-id"] || g.backupid || ""); },
    togglePbsGroup(id) {
      const i = this.job.pbs_backup_ids.indexOf(id);
      if (i >= 0) this.job.pbs_backup_ids.splice(i, 1); else this.job.pbs_backup_ids.push(id);
    },

    // ---- settings ---------------------------------------------------------
    async loadPrefs() { try { this.prefs = await api.get("/api/prefs"); } catch (e) { this.toast(e.message, "error"); } },
    async savePrefs() {
      this.busy = true;
      try { await api.put("/api/prefs", this.prefs); this.toast("Settings saved"); }
      catch (e) { this.toast("Save failed: " + e.message, "error"); }
      this.busy = false;
    },

    // ---- logs -------------------------------------------------------------
    async loadLogs() {
      try {
        const [runs, main] = await Promise.all([
          api.get("/api/logs/runs"), api.get("/api/logs/main?lines=200"),
        ]);
        this.runs = runs;
        this.logLines = [];
        this.pushLog(main.text);
        this.scrollLog();
      } catch (e) { console.error(e); }
    },
    pushLog(text) {
      for (const line of String(text).split("\n")) {
        if (!line.trim()) continue;
        this.logLines.push({ level: logLevelOf(line), text: line });
      }
      if (this.logLines.length > 1000) this.logLines.splice(0, this.logLines.length - 1000);
    },
    // Daemon log filtered to the selected minimum level (DEBUG hidden unless chosen).
    get visibleLog() {
      const min = LOG_LEVELS[this.logLevel] ?? 1;
      return this.logLines
        .filter((l) => (LOG_LEVELS[l.level] ?? 1) >= min)
        .map((l) => l.text)
        .join("\n");
    },
    // Same as visibleLog, but the [LEVEL] token is wrapped in a coloured span.
    // The whole line is HTML-escaped first, so log text can never inject markup.
    get visibleLogHtml() {
      const min = LOG_LEVELS[this.logLevel] ?? 1;
      const esc = (s) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
      return this.logLines
        .filter((l) => (LOG_LEVELS[l.level] ?? 1) >= min)
        .map((l) => esc(l.text).replace(/\[(DEBUG|INFO|WARN|ERROR|CRIT)\]/,
          (m, lv) => `<span class="lvl lvl-${lv.toLowerCase()}">${m}</span>`))
        .join("\n");
    },
    // Run logs grouped by job name; newest run first within each group.
    get runGroups() {
      const m = {};
      for (const r of this.runs) (m[r.name] || (m[r.name] = [])).push(r);
      return Object.keys(m).sort().map((name) => ({
        name,
        runs: m[name].slice().sort((a, b) => (a.date < b.date ? 1 : a.date > b.date ? -1 : 0)),
      }));
    },
    toggleGroup(name) { this.openGroups[name] = !this.openGroups[name]; },
    async openRun(file) {
      try { const r = await api.get("/api/logs/runs/" + encodeURIComponent(file)); this.runText = r.text; this.runOpen = true; }
      catch (e) { console.error(e); }
    },

    // ---- script editor ----------------------------------------------------
    async openScript(field) {
      this.scriptField = field;
      this.scriptPath = this.job[field] || "";
      this.scriptContent = "";
      if (this.scriptPath) {
        try { const r = await api.get("/api/scripts?path=" + encodeURIComponent(this.scriptPath)); this.scriptContent = r.content; } catch (e) {}
      } else {
        // suggest a timestamped name under the configured scripts directory
        if (!this.prefs) { try { this.prefs = await api.get("/api/prefs"); } catch (e) {} }
        const dir = this.prefs && this.prefs.paths ? this.prefs.paths.scripts : "";
        if (dir) {
          const suffix = field === "scriptAfterBackup" ? "afterbackup" : "beforebackup";
          this.scriptPath = dir.replace(/\/$/, "") + "/" + this.tsStamp() + "_" + suffix + ".sh";
        }
      }
      this.scriptOpen = true;
    },
    tsStamp() {
      const d = new Date(), p = (n) => String(n).padStart(2, "0");
      return "" + d.getFullYear() + p(d.getMonth() + 1) + p(d.getDate()) + p(d.getHours()) + p(d.getMinutes()) + p(d.getSeconds());
    },
    async pickScriptPath() {
      const p = await this.openPicker("script", { files: true });
      if (p) this.scriptPath = p;
    },
    insertMntVar() { this.scriptContent += "%MNTBACKUPDIR%"; },
    copyMntVar() {
      if (navigator.clipboard) navigator.clipboard.writeText("%MNTBACKUPDIR%").then(() => this.toast("Copied to clipboard"));
      else this.toast("Clipboard unavailable", "error");
    },
    async saveScript() {
      if (!this.scriptPath) { this.toast("Script path required", "error"); return; }
      try {
        await api.put("/api/scripts", { path: this.scriptPath, content: this.scriptContent });
        this.job[this.scriptField] = this.scriptPath;
        this.scriptOpen = false; this.toast("Script saved");
      } catch (e) { this.toast("Save failed: " + e.message, "error"); }
    },

    // ---- path picker (native-style file browser) --------------------------
    openPicker(mode, opts) {
      opts = opts || {};
      this.pickerMode = mode;
      this.pickerUuid = opts.uuid || "";
      this.pickerFiles = !!opts.files;
      this.pickerPath = ""; this.pickerSel = "";
      this.pickerSort = { key: "name", dir: 1 };
      this.pickerOpen = true;
      this.pickerLoad("");
      return new Promise((resolve) => { this.pickerResolve = resolve; });
    },
    async pickerLoad(path) {
      const q = new URLSearchParams({ root: this.pickerMode, path: path || "" });
      if (this.pickerUuid) q.set("uuid", this.pickerUuid);
      if (this.pickerFiles) q.set("files", "1");
      try {
        const r = await api.get("/api/browse?" + q.toString());
        this.pickerBase = r.base; this.pickerPath = r.path; this.pickerParent = r.parent;
        this.pickerEntries = r.entries; this.pickerSel = "";
      } catch (e) { this.toast("Browse: " + e.message, "error"); }
    },
    pickerEnter(name) { this.pickerLoad(this.pickerPath.replace(/\/$/, "") + "/" + name); },
    pickerActivate(e) {
      if (e.isDir) this.pickerEnter(e.name);
      else if (this.pickerFiles) this.pickerChoose(e.name);
    },
    pickerPrimary() {
      const s = this.pickerSelEntry;
      if (this.pickerFiles) { if (s && !s.isDir) this.pickerChoose(s.name); }
      else { if (s && s.isDir) this.pickerChoose(s.name); else this.pickerChoose(""); }
    },
    pickerChoose(name) {
      const abs = name ? this.pickerPath.replace(/\/$/, "") + "/" + name : this.pickerPath;
      this.pickerOpen = false;
      if (this.pickerResolve) { this.pickerResolve(abs); this.pickerResolve = null; }
    },
    pickerCancel() { this.pickerOpen = false; if (this.pickerResolve) { this.pickerResolve(null); this.pickerResolve = null; } },

    get pickerSelEntry() { return this.pickerEntries.find((e) => e.name === this.pickerSel) || null; },
    get pickerPrimaryDisabled() {
      if (this.pickerFiles) { const s = this.pickerSelEntry; return !(s && !s.isDir); }
      return false;
    },
    get pickerSorted() {
      const arr = [...this.pickerEntries];
      const { key, dir } = this.pickerSort;
      arr.sort((a, b) => {
        if (a.isDir !== b.isDir) return a.isDir ? -1 : 1;   // folders first
        let r = 0;
        if (key === "size") r = (a.size || 0) - (b.size || 0);
        else if (key === "mtime") r = (a.mtime || 0) - (b.mtime || 0);
        else r = a.name.localeCompare(b.name);
        return r * dir;
      });
      return arr;
    },
    setSort(key) {
      if (this.pickerSort.key === key) this.pickerSort = { key, dir: this.pickerSort.dir * -1 };
      else this.pickerSort = { key, dir: 1 };
    },
    get pickerCrumbs() {
      const base = this.pickerBase || "/";
      const cur = this.pickerPath || base;
      const crumbs = [{ label: base === "/" ? "/" : (base.split("/").filter(Boolean).pop() || base), path: base }];
      if (cur !== base && cur.startsWith(base)) {
        const rest = cur.slice(base === "/" ? 1 : base.length).split("/").filter(Boolean);
        let acc = base === "/" ? "" : base;
        for (const seg of rest) { acc = acc + "/" + seg; crumbs.push({ label: seg, path: acc }); }
      }
      return crumbs;
    },
    get pickerPlaces() {
      if (this.pickerMode === "dest") return [{ label: "This disk", path: this.pickerBase }];
      if (this.pickerMode === "script") return [{ label: "Scripts", path: this.pickerBase }];
      return [
        { label: "Root", path: "/" },
        { label: "Home", path: "/root" },
        { label: "Media", path: "/media" },
        { label: "Mounts", path: "/mnt" },
        { label: "etc", path: "/etc" },
      ];
    },
    humanSize(n) {
      if (n == null) return "";
      if (n < 1024) return n + " B";
      const u = ["KB", "MB", "GB", "TB"]; let i = -1;
      do { n /= 1024; i++; } while (n >= 1024 && i < u.length - 1);
      return (n < 10 ? n.toFixed(1) : Math.round(n)) + " " + u[i];
    },
    fmtMtime(s) {
      if (!s) return "";
      const d = new Date(s * 1000);
      return d.toISOString().slice(0, 10) + " " + d.toTimeString().slice(0, 5);
    },

    // pickers wired to specific fields ----------------------------------------
    async pickSource() {
      const p = await this.openPicker("src", { files: false });
      if (p) this.folderSource = p;
    },
    async pickDest() {
      if (!this.editorMountDir) { this.toast("Mount the partition first (General tab)", "error"); return; }
      const p = await this.openPicker("dest", { uuid: this.job.partition_uuid, files: false });
      if (p) this.folderDest = this.toGeneric(p);
    },
    async pickPbsDest() {
      if (!this.editorMountDir) { this.toast("Mount the partition first (General tab)", "error"); return; }
      const p = await this.openPicker("dest", { uuid: this.job.partition_uuid, files: false });
      if (p) this.job.pbs_dest_folder = this.toGeneric(p);
    },
    async pickLuksFile() {
      const p = await this.openPicker("keyfile", { files: true });
      if (p) this.job.encLUKSFilePath = p;
    },
    toGeneric(abs) {
      if (this.editorMountDir && abs.startsWith(this.editorMountDir))
        return abs.replace(this.editorMountDir, "%MNTBACKUPDIR%");
      return abs;
    },

    async pickPbsKeyfile() {
      const p = await this.openPicker("keyfile", { files: true });
      if (p) this.pbsForm.keyfile = p;
    },
    async pickPrefPath(key) {
      const p = await this.openPicker("src", { files: false });
      if (p && this.prefs && this.prefs.paths) this.prefs.paths[key] = p;
    },
    fmtEpoch(s) {
      if (!s) return "—";
      const d = new Date(s * 1000);
      return d.toISOString().slice(0, 16).replace("T", " ");
    },
  };
}
