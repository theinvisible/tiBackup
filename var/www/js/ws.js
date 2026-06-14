/* tiBackup web UI — WebSocket live channel.
 * Connects to /ws (same origin), forwards parsed messages to a handler, and
 * reports open/close so the app can switch between live push and polling. */
(function () {
  "use strict";

  window.liveSocket = {
    sock: null,
    retry: null,

    connect(onMessage, onOpen, onClose) {
      this.close();
      const proto = location.protocol === "https:" ? "wss:" : "ws:";
      let ws;
      try {
        ws = new WebSocket(proto + "//" + location.host + "/ws");
      } catch (e) {
        if (onClose) onClose();
        return;
      }
      this.sock = ws;

      ws.onopen = () => { if (onOpen) onOpen(); };
      ws.onmessage = (ev) => {
        let msg;
        try { msg = JSON.parse(ev.data); } catch (e) { return; }
        if (onMessage) onMessage(msg);
      };
      ws.onclose = () => {
        this.sock = null;
        if (onClose) onClose();
      };
      ws.onerror = () => { try { ws.close(); } catch (e) {} };
    },

    close() {
      if (this.retry) { clearTimeout(this.retry); this.retry = null; }
      if (this.sock) {
        try { this.sock.onclose = null; this.sock.close(); } catch (e) {}
        this.sock = null;
      }
    },
  };
})();
