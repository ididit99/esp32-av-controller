const $ = (id) => document.getElementById(id);

window.onerror = function (message, source, lineno, colno) {
  try { $("statusOut").textContent = `JS ERROR:\n${message}\n${source}:${lineno}:${colno}`; } catch { }
};

function esc(s) { return (s || "").replaceAll("&", "&amp;").replaceAll("<", "&lt;"); }

async function apiGet(path) {
  const r = await fetch(path, { cache: "no-store" });
  const t = await r.text();
  if (!r.ok) throw new Error(t);
  try { return JSON.parse(t); } catch { throw new Error("Bad JSON: " + t); }
}

async function apiPost(path, obj) {
  const r = await fetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(obj)
  });
  const t = await r.text();
  let j;
  try { j = JSON.parse(t); } catch { j = { raw: t }; }
  if (!r.ok) throw new Error(j.error || t);
  return j;
}

function setupTabs() {
  const tabs = document.querySelectorAll(".tab");
  tabs.forEach(t => t.onclick = () => {
    tabs.forEach(x => x.classList.remove("active"));
    t.classList.add("active");
    const id = t.dataset.tab;
    document.querySelectorAll(".panel").forEach(p => p.classList.remove("show"));
    document.getElementById(id).classList.add("show");
  });
}

function setStatus(msg) { $("statusOut").textContent = msg; }

function rssiLabel(rssi) {
  if (rssi >= -50) return "Excellent";
  if (rssi >= -60) return "Good";
  if (rssi >= -70) return "OK";
  if (rssi >= -80) return "Weak";
  return "Very weak";
}

async function refreshHealth() {
  const h = await apiGet("/api/health");

  const sta = h.wifi.staConnected
    ? `STA ✓ ${h.wifi.staIp} (${h.wifi.staSsid}) RSSI ${h.wifi.rssi}`
    : "STA ✗ disconnected";

  $("healthLine").textContent =
    `FW ${h.fw} | ${sta} | AP ${h.wifi.apIp} (${h.wifi.apSsid || "AP SSID not set"}) | Uptime ${h.uptime_s}s | Heap ${h.heap_free}`;

  // Big obvious line on the Wi-Fi page
  if (h.wifi.staConnected) {
    $("wifiConnLine").textContent =
      `Connected to ${h.wifi.staSsid} — LAN IP ${h.wifi.staIp} (RSSI ${h.wifi.rssi} / ${rssiLabel(h.wifi.rssi)})`;
  } else {
    $("wifiConnLine").textContent =
      `Not connected to LAN Wi-Fi. AP is ${h.wifi.apIp} (${h.wifi.apSsid || "no AP SSID"})`;
  }

  $("learnEnabled").checked = !!h.learn.enabled;
  $("learnPort").value = h.learn.port;
}

async function loadWifiForm() {
  const w = await apiGet("/api/wifi");
  $("wMode").value = w.mode || "apsta";
  $("wStaSsid").value = w.staSsid || "";
  $("wApSsid").value = w.apSsid || "";
  $("wApCh").value = w.apChan ?? 6;
  // passwords intentionally not filled
}

function populateScan(res) {
  const sel = $("wifiScanList");
  sel.options.length = 0;
  sel.add(new Option("(select a network)", ""));

  // Debug: check elements
  if (!sel) {
    alert("Error: wifiScanList element not found!");
    return;
  }

  const nets = Array.isArray(res.networks) ? res.networks : [];

  try {
    for (let i = 0; i < nets.length; i++) {
      const nw = nets[i];
      if (!nw.ssid) continue;

      const lock = nw.open ? "open" : "locked";
      const label = `${nw.ssid} • ${nw.rssi || 0}dBm • ch${nw.chan || 0} • ${lock}`;

      const opt = document.createElement("option");
      opt.value = nw.ssid;
      opt.textContent = label;
      sel.appendChild(opt);
    }
  } catch (e) {
    setStatus("Error rendering list: " + e.message);
  }

  if (!nets.length) setStatus("No networks found (UI).\nRaw: " + JSON.stringify(res));
  else setStatus(`Found ${nets.length} networks.\n` + (res.note || ""));
}

async function scanCached() {
  setStatus("Loading cached scan...");
  try {
    populateScan(await apiGet("/api/wifi/scan"));
  } catch (e) {
    setStatus("Cached scan failed: " + e.message);
  }
}

async function scanFresh() {
  setStatus("Starting fresh scan (this may take 5s)...");
  try {
    populateScan(await apiGet("/api/wifi/scan?fresh=1"));
  } catch (e) {
    setStatus("Fresh scan failed: " + e.message);
  }
}

// ---------- Live Log WS ----------
let wsLog;
function logLine(s) {
  const el = $("log");
  const div = document.createElement("div");
  div.textContent = s;
  el.appendChild(div);
  el.scrollTop = el.scrollHeight;
}
function connectLogWs() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  wsLog = new WebSocket(`${proto}://${location.host}/ws`);
  wsLog.onmessage = (e) => logLine(e.data);
  wsLog.onclose = () => setTimeout(connectLogWs, 1000);
}

// ---------- Terminal WS ----------
let wsTerm;
function termLine(html) {
  const el = $("termOut");
  const div = document.createElement("div");
  div.innerHTML = html;
  el.appendChild(div);
  el.scrollTop = el.scrollHeight;
}
function connectTermWs() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  wsTerm = new WebSocket(`${proto}://${location.host}/term`);
  wsTerm.onmessage = (e) => {
    try {
      const msg = JSON.parse(e.data);
      if (msg.type === "status") termLine(`<span class="muted">STATUS:</span> connected=${msg.connected} ${esc(msg.host || "")}:${esc(String(msg.port || ""))}`);
      else if (msg.type === "rx") termLine(`<span class="rx">RX</span> <span class="mono">${esc(msg.hex)}</span><div class="mono small">${esc(msg.ascii)}</div>`);
      else if (msg.type === "tx") termLine(`<span class="tx">TX</span> ok`);
      else if (msg.type === "error") termLine(`<span class="err">ERR</span> ${esc(msg.msg)}`);
      else termLine(`<span class="muted">${esc(e.data)}</span>`);
    } catch {
      termLine(`<span class="muted">${esc(e.data)}</span>`);
    }
  };
  wsTerm.onclose = () => setTimeout(connectTermWs, 1000);
}

// ---------- Proxy WS ----------
let wsProxy;
function proxyLine(html) {
  const el = $("proxyOut");
  const div = document.createElement("div");
  div.innerHTML = html;
  el.appendChild(div);
  el.scrollTop = el.scrollHeight;
}
function connectProxyWs() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  wsProxy = new WebSocket(`${proto}://${location.host}/wsproxy`);
  wsProxy.onmessage = (e) => {
    try {
      const msg = JSON.parse(e.data);
      if (msg.type === "status") proxyLine(`<span class="muted">STATUS:</span> running=${msg.running} connected=${msg.connected ?? ""} listen=${msg.listenPort ?? ""} target=${esc(msg.targetHost || "")}:${msg.targetPort || ""} capToLearn=${msg.captureToLearn ?? ""}`);
      else if (msg.type === "data") proxyLine(`<span class="mono small">${esc(msg.dir)}</span><div><span class="mono">${esc(msg.hex)}</span></div><div class="mono small">${esc(msg.ascii)}</div>`);
      else if (msg.type === "error") proxyLine(`<span class="err">ERR</span> ${esc(msg.msg)}`);
      else proxyLine(`<span class="muted">${esc(e.data)}</span>`);
    } catch {
      proxyLine(`<span class="muted">${esc(e.data)}</span>`);
    }
  };
  wsProxy.onclose = () => setTimeout(connectProxyWs, 1000);
}

// ---------- Discovery WS + UI ----------
let wsDisc;
let discRows = [];

function mkTermButtons(ip, openPorts, suggestedSuffix) {
  let out = "";
  (openPorts || []).forEach(p => {
    if ([23, 23001, 8080, 5000, 6100, 1515, 41794].includes(p) || p < 100) {
      out += `<button class="btn tiny" data-term="${esc(ip)}" data-port="${p}" data-suf="${esc(suggestedSuffix || "")}">Terminal:${p}</button> `;
    }
  });
  if ((openPorts || []).includes(80)) out += `<a class="btn tiny" href="http://${ip}/" target="_blank" rel="noopener">HTTP</a> `;
  if ((openPorts || []).includes(443)) out += `<a class="btn tiny" href="https://${ip}/" target="_blank" rel="noopener">HTTPS</a> `;
  return out.trim();
}

function renderDisc() {
  const box = $("discOut");
  if (!discRows.length) {
    box.textContent = "(no results yet)";
    return;
  }

  let html = "";
  discRows.slice(0).reverse().forEach(r => {
    const ip = r.ip;
    const ports = (r.openPorts || []).join(", ");
    const banner = r.banner || r.fingerprint || "";
    const tpl = r.suggestedTemplateId || r.templateId || "";
    const suf = r.suggestedSuffix || r.suffix || "";
    const sugPort = r.suggestedPort || 0;

    html += `<div class="discRow">
      <div>
        <b>${esc(ip)}</b>
        <div class="mono small">ports: ${esc(ports || "(none)")}</div>
        ${banner ? `<div class="small"><span class="pill">banner</span> ${esc(banner)}</div>` : ""}
        ${(tpl || suf) ? `<div class="mono small">${tpl ? `template: <b>${esc(tpl)}</b>` : ""} ${suf ? `suffix: <b>${esc(suf)}</b>` : ""}</div>` : ""}
        ${r.mac ? `<div class="xsmall mono">MAC: ${esc(r.mac)}</div>` : ""}
      </div>
      <div class="row">
        ${mkTermButtons(ip, r.openPorts || [], suf)}
        <button class="btn tiny" data-save="${esc(ip)}"
                data-ports="${esc(JSON.stringify(r.openPorts || []))}"
                data-sugport="${sugPort}"
                data-banner="${esc(banner)}"
                data-namehint="${esc(r.nameHint || "")}"
                data-tpl="${esc(tpl)}"
                data-mac="${esc(r.mac || "")}"
                data-suf="${esc(suf)}">Save</button>
      </div>
    </div>`;
  });

  box.innerHTML = html;

  box.querySelectorAll("[data-save]").forEach(btn => {
    btn.onclick = async () => {
      const ip = btn.dataset.save;
      const openPorts = JSON.parse(btn.dataset.ports || "[]");
      const sugPort = Number(btn.dataset.sugport || 0);
      const portHint = sugPort || (openPorts.length ? openPorts[0] : 0);
      const tpl = btn.dataset.tpl || "";
      const suf = btn.dataset.suf || "";
      const mac = btn.dataset.mac || "";

      const name = btn.dataset.namehint || btn.dataset.banner || ip;

      if (!confirm(`Save device?\nName: ${name}\nIP: ${ip}`)) return;

      await apiPost("/api/devices/add", {
        name, ip, portHint,
        defaultSuffix: suf,
        siteId: "Discovered",
        roomId: "",
        notes: "Auto-saved from Discovery",
        templateId: tpl,
        mac: mac
      });

      alert("Saved. See Devices tab.");
    };
  });
}

function connectDiscWs() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  wsDisc = new WebSocket(`${proto}://${location.host}/wsdisc`);
  wsDisc.onmessage = (e) => {
    try {
      const msg = JSON.parse(e.data);
      if (msg.type === "done") return;
      if (msg.ip) {
        discRows.push(msg);
        renderDisc();
      }
    } catch { }
  };
  wsDisc.onclose = () => setTimeout(connectDiscWs, 1000);
}

async function discRefreshSnapshot() {
  const snap = await apiGet("/api/discovery/results");
  discRows = snap.results || [];
  renderDisc();
}

// ---------- Learn Captures ----------
async function refreshCaps() {
  const filter = $("capFilter").value.trim();
  const pinned = $("capPinnedOnly").checked ? "1" : "0";
  const url = `/api/captures?filter=${encodeURIComponent(filter)}&pinned=${pinned}`;
  const res = await apiGet(url);

  const wrap = $("caps");
  wrap.innerHTML = "";

  (res.captures || []).slice(0, 80).forEach(c => {
    const el = document.createElement("div");
    el.className = "item";
    el.innerHTML = `
      <div class="row between">
        <div>
          <b class="mono">${esc(c.id)}</b>
          <div class="mono small">${esc(c.srcIp)}:${c.srcPort} (local ${c.localPort}) ${c.pinned ? "📌" : ""} ${c.repeats > 1 ? "x" + c.repeats : ""}</div>
          <div class="mono small">hint: <b>${esc(c.suffixHint || "(none)")}</b> [${esc(c.payloadType || "?")}]</div>
        </div>
        <div class="row">
          <button class="btn tiny" data-use="${esc(c.id)}">Use</button>
          <button class="btn tiny" data-pin="${esc(c.id)}">${c.pinned ? "Unpin" : "Pin"}</button>
        </div>
      </div>
      <div class="mono small">HEX: ${esc(c.hex)}</div>
      <div class="mono small">ASCII: ${esc(c.ascii)}</div>
    `;

    el.querySelector("[data-pin]").onclick = async () => {
      await apiPost("/api/capture/pin", { id: c.id, pin: !c.pinned });
      await refreshCaps();
    };

    el.querySelector("[data-use]").onclick = () => {
      $("saveCapId").value = c.id;
      $("learnSaveOut").textContent = "Capture selected. Click “Load capture”.";
    };

    wrap.appendChild(el);
  });
}

// ---------- Devices ----------
async function loadDevices() {
  const devs = await apiGet("/api/devices");
  const wrap = $("devicesList");
  wrap.innerHTML = "";

  (devs || []).slice().reverse().forEach(d => {
    const ip = d.ip || "";
    const port = d.portHint || "";
    const suffix = d.defaultSuffix || "";
    const notes = d.notes || "";
    const tpl = d.templateId || "";

    const el = document.createElement("div");
    el.className = "item";
    el.innerHTML = `
      <div class="row between">
        <div>
          <b>${esc(d.name || ip)}</b>
          <div class="mono small">${esc(ip)}:${esc(String(port))} suffix=${esc(suffix || "(none)")}</div>
          <div class="small">${esc(notes)}</div>
          ${d.mac ? `<div class="xsmall mono">MAC: ${esc(d.mac)}</div>` : ""}
        </div>
        <div class="row">
          ${d.mac ? `<button class="btn tiny" data-wake="${esc(d.mac)}">Wake</button>` : ""}
          <button class="btn tiny" data-open-term="${esc(ip)}" data-port="${esc(String(port))}" data-suf="${esc(suffix)}">Terminal</button>
          <a class="btn tiny" href="http://${ip}/" target="_blank" rel="noopener">HTTP</a>
          <button class="btn tiny danger" data-del="${esc(d.id)}">Del</button>
        </div>
      </div>
    `;
    el.querySelector("[data-open-term]").onclick = () => {
      $("termHost").value = ip;
      $("termPort").value = port || "";
      $("termSuffix").value = suffix || "";
      document.querySelector(`.tab[data-tab="tab-terminal"]`).click();
    };
    if (el.querySelector("[data-wake]")) {
      el.querySelector("[data-wake]").onclick = async () => {
        if (confirm("Send Wake-on-LAN packet to " + d.mac + "?")) {
          await apiPost("/api/wol", { mac: d.mac });
        }
      };
    };
    el.querySelector("[data-del]").onclick = async () => {
      if (!confirm(`Delete device "${d.name}"?`)) return;
      try {
        await apiPost("/api/devices/delete", { id: d.id });
        await loadDevices();
      } catch (e) {
        alert("Delete failed: " + e.message);
      }
    };
    wrap.appendChild(el);
  });
}

// ---------- Backup ----------
function downloadText(filename, text) {
  const blob = new Blob([text], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

window.onload = async () => {
  setupTabs();
  connectLogWs();
  connectTermWs();
  connectProxyWs();
  connectDiscWs();

  await loadWifiForm();
  await refreshHealth();
  setInterval(refreshHealth, 2000);

  // Wi-Fi
  scanCached().catch(e => setStatus("Scan failed: " + e.message));
  $("btnWifiScan").onclick = () => scanCached().catch(e => setStatus("Scan failed: " + e.message));
  $("btnWifiRefresh").onclick = () => scanFresh().catch(e => setStatus("Scan failed: " + e.message));

  $("btnUseSsid").onclick = () => {
    const ssid = $("wifiScanList").value;
    if (!ssid) return;
    $("wStaSsid").value = ssid;
    $("wStaPass").focus();
    setStatus("Filled STA SSID: " + ssid);
  };

  $("btnSaveWifi").onclick = async () => {
    try {
      const res = await apiPost("/api/wifi", {
        mode: $("wMode").value,
        staSsid: $("wStaSsid").value,
        staPass: $("wStaPass").value,
        apSsid: $("wApSsid").value,
        apPass: $("wApPass").value,
        apChan: Number($("wApCh").value || 6),
      });
      setStatus("Saved.\n" + JSON.stringify(res, null, 2));
    } catch (e) {
      setStatus("Save failed: " + e.message);
    }
  };

  $("btnForgetWifi").onclick = async () => {
    if (!confirm("Are you sure? This will clear Wi-Fi settings and reboot into AP mode.")) return;
    try {
      const res = await apiPost("/api/wifi/forget", {});
      setStatus("Result: " + res.note);
    } catch (e) {
      setStatus("Error: " + e.message);
    }
  };

  // Terminal
  $("btnTermConnect").onclick = () => {
    const host = $("termHost").value.trim();
    const port = Number($("termPort").value);
    wsTerm.send(JSON.stringify({ action: "connect", host, port }));
  };
  $("btnTermDisconnect").onclick = () => wsTerm.send(JSON.stringify({ action: "disconnect" }));
  $("btnTermSend").onclick = () => {
    const mode = $("termMode").value;
    const suffix = $("termSuffix").value;
    const data = $("termSend").value.trim();
    wsTerm.send(JSON.stringify({ action: "send", mode, suffix, data }));
  };

  // Discovery
  $("btnDiscStart").onclick = async () => {
    discRows = [];
    renderDisc();
    const subnet = $("discSubnet").value.trim();
    const from = Number($("discFrom").value || 1);
    const to = Number($("discTo").value || 254);
    const portsCsv = $("discPorts").value.trim();
    const ports = portsCsv.split(",").map(x => Number(x.trim())).filter(n => n > 0 && n < 65536);
    await apiPost("/api/discovery/start", { subnet, from, to, ports });
    await discRefreshSnapshot();
  };
  $("btnDiscStop").onclick = async () => {
    await apiPost("/api/discovery/stop", {});
  };
  $("btnDiscRefresh").onclick = discRefreshSnapshot;

  // Allow clicking “Terminal:port” buttons inside discovery list
  $("discOut").addEventListener("click", (ev) => {
    const t = ev.target;
    if (t && t.dataset && t.dataset.term) {
      $("termHost").value = t.dataset.term;
      $("termPort").value = t.dataset.port;
      if (t.dataset.suf) $("termSuffix").value = t.dataset.suf;
      document.querySelector(`.tab[data-tab="tab-terminal"]`).click();
      wsTerm.send(JSON.stringify({ action: "connect", host: t.dataset.term, port: Number(t.dataset.port) }));
    }
  });

  // Proxy
  $("btnProxyStart").onclick = async () => {
    const listenPort = Number($("proxyListen").value || 23001);
    const targetHost = $("proxyTargetHost").value.trim();
    const targetPort = Number($("proxyTargetPort").value || 0);
    const captureToLearn = $("proxyCapToLearn").checked;
    await apiPost("/api/proxy/start", { listenPort, targetHost, targetPort, captureToLearn });
  };
  $("btnProxyStop").onclick = async () => {
    await apiPost("/api/proxy/stop", {});
  };

  // Learn
  $("btnSaveLearner").onclick = async () => {
    await apiPost("/api/learner", { enabled: $("learnEnabled").checked, port: Number($("learnPort").value) });
    await refreshHealth();
    await refreshCaps();
  };
  $("btnRefreshCaps").onclick = refreshCaps;
  await refreshCaps();

  // Save device from capture
  $("btnLoadCap").onclick = async () => {
    const id = $("saveCapId").value.trim();
    if (!id) return;
    const cap = await apiGet(`/api/capture/get?id=${encodeURIComponent(id)}`);
    $("devIp").value = cap.srcIp || "";
    $("devPortHint").value = cap.srcPort || "";
    $("devSuffix").value = cap.suffixHint || "";
    $("devName").value = cap.srcIp || "Device";
    $("devPayloadType").value = cap.payloadType || "";
    $("learnSaveOut").textContent = `Loaded capture ${cap.id}. suffix hint: ${cap.suffixHint || "(none)"}`;
  };

  $("btnSaveDevice").onclick = async () => {
    try {
      const res = await apiPost("/api/devices/add", {
        name: $("devName").value.trim() || $("devIp").value.trim(),
        ip: $("devIp").value.trim(),
        portHint: Number($("devPortHint").value || 0),
        defaultSuffix: $("devSuffix").value,
        siteId: $("devSite").value.trim(),
        roomId: $("devRoom").value.trim(),
        notes: $("devNotes").value,
        templateId: $("devTemplate").value.trim(),
        defaultPayloadType: $("devPayloadType").value,
        mac: $("devMac").value.trim()
      });
      $("learnSaveOut").textContent = "Saved device.\n" + JSON.stringify(res, null, 2);
    } catch (e) {
      $("learnSaveOut").textContent = "Save failed: " + e.message;
    }
  };

  // Devices
  await loadDevices();

  // ---------- mDNS ----------
  async function scanMdns() {
    const box = $("mdnsOut");
    box.textContent = "Scanning...";
    try {
      const res = await apiPost("/api/mdns/scan", {
        service: $("mdnsService").value,
        proto: $("mdnsProto").value
      });

      if (!res.results || !res.results.length) {
        box.textContent = "No devices found.";
        return;
      }

      let html = "";
      res.results.forEach(r => {
        html += `<div class="discRow">
          <div>
            <b>${esc(r.hostname)}</b>
            <div class="mono small">${esc(r.ip)}:${r.port}</div>
          </div>
          <button class="btn tiny" data-save="${esc(r.ip)}" data-name="${esc(r.hostname)}">Save</button>
        </div>`;
      });
      box.innerHTML = html;

      box.querySelectorAll("[data-save]").forEach(btn => {
        btn.onclick = async () => {
          const ip = btn.dataset.save;
          const name = btn.dataset.name;
          if (!confirm(`Save mDNS device?\nName: ${name}\nIP: ${ip}`)) return;

          await apiPost("/api/devices/add", {
            name, ip, portHint: 0,
            defaultSuffix: "\\r",
            siteId: "Discovered",
            roomId: "",
            notes: "Auto-saved from mDNS",
            templateId: ""
          });
          alert("Saved.");
        };
      });

    } catch (e) {
      box.textContent = "Error: " + e.message;
    }
  }

  // ---------- PJLink ----------
  window.sendPjl = async (cmd) => {
    const ip = $("pjlIp").value.trim();
    const pass = $("pjlPass").value.trim();

    if (!ip) {
      $("pjlOut").textContent = "Error: IP required";
      return;
    }

    $("pjlOut").textContent = "Sending " + cmd + "...";
    try {
      const res = await apiPost("/api/pjlink", { ip, pass, cmd });
      $("pjlOut").textContent = "RW: " + res.response;
    } catch (e) {
      $("pjlOut").textContent = "Error: " + e.message;
    }
  };

  // ---------- Diagnostics ----------
  $("btnPing").onclick = async () => {
    const host = $("pingHost").value;
    const box = $("pingOut");
    box.insertAdjacentHTML("beforeend", `<div>> Pinging ${esc(host)}...</div>`);
    try {
      const res = await apiGet("/api/ping?host=" + encodeURIComponent(host));
      const color = res.ok ? "var(--c-accent)" : "var(--c-danger)";
      const text = res.ok ? `Reply ${res.avg_time_ms}ms` : "Timeout";
      box.insertAdjacentHTML("beforeend", `<div style="color:${color}">${esc(text)}</div>`);
      box.scrollTop = box.scrollHeight;
    } catch (e) {
      box.insertAdjacentHTML("beforeend", `<div class="err">Error: ${esc(e.message)}</div>`);
    }
  };

  $("btnSsdp").onclick = async () => {
    const box = $("ssdpOut");
    box.textContent = "Sending M-SEARCH (2s wait)...";
    try {
      const res = await apiGet("/api/ssdp/scan");
      if (!res.length) {
        box.textContent = "No SSDP replies.";
        return;
      }
      let h = "";
      res.forEach(r => {
        h += `<div class="discRow">
                <div>
                  <b>${esc(r.srv || r.loc || "Unknown")}</b>
                  <div class="mono small">${esc(r.ip)}</div>
                  <div class="xsmall muted">${esc(r.usn || "")}</div>
                </div>
              </div>`;
      });
      box.innerHTML = h;
    } catch (e) {
      box.textContent = "Error: " + e.message;
    }
  };

  $("btnSubnetScan").onclick = async () => {
    if (!confirm("Start Deep Scan? This interrupts other tasks.")) return;
    try {
      await apiPost("/api/scan/subnet", {});
      $("discOut2").innerText = "Scan started... Watch 'Discovery' tab or wait here (results shared).";

      // Poll /api/scan/results or just watch discOut
      setInterval(() => {
        if ($("discOut").innerHTML)
          $("discOut2").innerHTML = $("discOut").innerHTML;
      }, 1000);
    } catch (e) {
      alert(e.message);
    }
  };

  window.sendPjlInput = () => {
    const val = $("pjlInput").value;
    sendPjl("%1INPT " + val);
  };

  window.sendPjlCustom = () => {
    const val = $("pjlCustom").value.trim();
    if (val) sendPjl(val);
  };

  // ---------- mDNS Buttons ----------
  $("mdnsService").onchange = () => {
    $("mdnsCustom").style.display = ($("mdnsService").value === "custom") ? "inline-block" : "none";
  };

  $("btnMdnsScan").onclick = async () => {
    // Use custom value if selected, otherwise dropdown value
    const svcVal = $("mdnsService").value;
    const service = (svcVal === "custom") ? $("mdnsCustom").value.trim() : svcVal;

    // Hack: Pass updated service to the original function logic if we can, 
    // but the original function pulls directly from the DOM.
    // Let's redefine the onclick to call a slightly modified scanMdns or just update the DOM before calling.
    // Actually, let's just update the scanMdns function itself.
    await scanMdnsWithArgs(service, $("mdnsProto").value);
  };

  // Refactored mDNS scan to take args
  async function scanMdnsWithArgs(service, proto) {
    const box = $("mdnsOut");
    box.textContent = "Scanning " + service + " (" + proto + ")...";
    try {
      const res = await apiPost("/api/mdns/scan", { service, proto });
      if (!res.results || !res.results.length) {
        box.textContent = "No devices found for " + service;
        return;
      }

      let html = "";
      res.results.forEach(r => {
        html += `<div class="discRow">
          <div>
            <b>${esc(r.hostname)}</b>
            <div class="mono small">${esc(r.ip)}:${r.port}</div>
          </div>
          <button class="btn tiny" data-save="${esc(r.ip)}" data-name="${esc(r.hostname)}">Save</button>
        </div>`;
      });
      box.innerHTML = html;

      box.querySelectorAll("[data-save]").forEach(btn => {
        btn.onclick = async () => {
          const ip = btn.dataset.save;
          const name = btn.dataset.name;
          if (!confirm(`Save mDNS device?\nName: ${name}\nIP: ${ip}`)) return;
          await apiPost("/api/devices/add", {
            name, ip, portHint: 0, defaultSuffix: "\\r",
            siteId: "Discovered", roomId: "", notes: "Auto-saved from mDNS", templateId: ""
          });
          alert("Saved.");
        };
      });
    } catch (e) {
      box.textContent = "Error: " + e.message;
    }
  }

  // $("btnMdnsScan").onclick = scanMdns; // Replaced above
  $("btnWolSend").onclick = async () => {
    const mac = $("wolMac").value.trim();
    if (!mac) return;
    await apiPost("/api/wol", { mac });
    alert("WoL packet sent.");
  };

  // Backup
  $("btnLoadCfg").onclick = async () => {
    $("cfgText").value = await fetch("/api/config", { cache: "no-store" }).then(r => r.text());
    $("cfgOut").textContent = "Loaded.";
  };
  $("btnDownloadCfg").onclick = () => downloadText("esp32-av-tool-config.json", $("cfgText").value);
  $("btnUploadCfg").onclick = async () => {
    const txt = $("cfgText").value;
    const r = await fetch("/api/config", { method: "POST", headers: { "Content-Type": "application/json" }, body: txt });
    $("cfgOut").textContent = await r.text();
  };

  $("btnReboot").onclick = async () => {
    try { await apiPost("/api/reboot", {}); } catch { }
  };

  // Update / Rollback support
  $("btnOpenUpdate").onclick = () => {
    document.querySelector(`.tab[data-tab="tab-update"]`).click();
  };

  async function performUpdate(fileInputId, type, progId) {
    const info = $(progId);
    const file = $(fileInputId).files[0];
    if (!file) {
      alert("Select a file first.");
      return;
    }
    info.style.display = "block";
    info.firstElementChild.style.width = "0%";

    // Generic XHR for progress
    const xhr = new XMLHttpRequest();
    xhr.open("POST", "/update?type=" + type);
    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable) {
        const pct = Math.round((e.loaded / e.total) * 100);
        info.firstElementChild.style.width = pct + "%";
      }
    };
    xhr.onload = () => {
      if (xhr.status === 200) {
        info.firstElementChild.style.width = "100%";
        $("otaOut").textContent = "Update complete. Rebooting...";
      } else {
        $("otaOut").textContent = "Error: " + xhr.status + " " + xhr.responseText;
      }
    };
    xhr.onerror = () => {
      $("otaOut").textContent = "Network Error";
    };

    const formData = new FormData();
    formData.append("update", file);
    xhr.send(formData);
  }

  $("btnUpdateFw").onclick = () => performUpdate("fileFw", "firmware", "progFw");
  $("btnUpdateFs").onclick = () => performUpdate("fileFs", "fs", "progFs");

  $("btnRollback").onclick = async () => {
    if (!confirm("Confirm rollback to previous firmware version?")) return;
    try {
      const res = await apiPost("/api/rollback", {});
      $("otaOut").textContent = res.note || "Rolled back.";
    } catch (e) {
      $("otaOut").textContent = "Rollback failed: " + e.message;
    }
  };
};
