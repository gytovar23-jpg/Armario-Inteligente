const LIVE_POINTS = 120;
const live = { labels: [], c0: [], c1: [], heap: [] };
const TASK_STATES = ["Rodando", "Pronta", "Bloqueada", "Suspensa", "Removida"];

const cCpu = lineChart("cCpu", [{ label: "Core 0", data: [], borderColor: COLORS[0] }, { label: "Core 1", data: [], borderColor: COLORS[1] }], { max: 100 });
const cHeap = lineChart("cHeap", [{ data: [], borderColor: COLORS[2] }], { beginAtZero: false });
const cSens = lineChart("cSens", [{ label: "Destrancado", data: [], borderColor: COLORS[1] }, { label: "Slot 1", data: [], borderColor: COLORS[0] }, { label: "Slot 2", data: [], borderColor: COLORS[2] }], { max: 100 });
const cNfc = lineChart("cNfc", [{ data: [], backgroundColor: COLORS[3] }], { type: "bar" });
const cCpu24 = lineChart("cCpu24", [{ label: "Core 0", data: [], borderColor: COLORS[0] }, { label: "Core 1", data: [], borderColor: COLORS[1] }], { max: 100 });
const cNet24 = lineChart("cNet24", [{ label: "Heap (KB)", data: [], borderColor: COLORS[2] }, { label: "RSSI (dBm)", data: [], borderColor: COLORS[3] }], { beginAtZero: false });

function dl(id, rows) {
  $(id).innerHTML = rows.map(([k, v]) => `<dt>${k}</dt><dd>${v}</dd>`).join("");
}

function signalBars(bars) {
  const heights = [5, 8, 11, 14];
  return `<span class="signal">${heights.map((h, i) => `<i class="${i < bars ? "on" : ""}" style="height:${h}px"></i>`).join("")}</span>`;
}

function fmtUptime(s) {
  const d = Math.floor(s / 86400);
  return `${d ? `${d} d ` : ""}${pad(Math.floor(s / 3600) % 24)}:${pad(Math.floor(s / 60) % 60)}:${pad(s % 60)}`;
}

function render(p) {
  const kpi = (value, label) => `<div class="kpi"><b>${value}</b><span>${label}</span></div>`;
  $("kpis").innerHTML = kpi(`${p.cpu[0]}%`, "CPU core 0") + kpi(`${p.cpu[1]}%`, "CPU core 1") + kpi(fmtKb(p.mem.heapFree), "Heap livre")
    + kpi(fmtUptime(p.uptimeSec), "Uptime") + kpi(`${p.cpuMhz} MHz`, "Frequência") + kpi(esc(p.firmware), "Firmware") + kpi(esc(p.resetReason), "Último reset");

  dl("mem", [
    ["Heap livre", fmtKb(p.mem.heapFree)], ["Heap total", fmtKb(p.mem.heapTotal)], ["Mínimo já atingido", fmtKb(p.mem.heapMin)],
    ["Maior bloco livre", fmtKb(p.mem.heapLargest)], ["Fragmentação", `${p.mem.fragPct}%`], ["PSRAM", "não disponível neste módulo"],
  ]);
  dl("flash", [
    ["Tamanho", fmtKb(p.flash.size)], ["App usado", `${fmtKb(p.flash.appUsed)} de ${fmtKb(p.flash.appMax)}`],
    ["LittleFS usado", fmtKb(p.flash.fsUsed)], ["LittleFS livre", fmtKb(p.flash.fsTotal - p.flash.fsUsed)],
    ["Logs guardados", `${p.logs.count} (perdidos: ${p.logs.dropped})`],
  ]);
  const w = p.wifi;
  dl("wifi", [
    ["Modo", esc(w.mode)], ["SSID", esc(w.ssid || "—")], ["IP", esc(w.ip || "—")], ["MAC", esc(w.mac)],
    ["Sinal", w.mode === "STA" ? `${signalBars(w.bars)}${w.rssi} dBm (${w.pct}%)` : "—"], ["Canal", w.channel || "—"],
    ["Tempo conectado", fmtUptime(w.sec)], ["Reconexões", w.reconnects],
    ["Ponto de acesso", w.ap ? `ativo, ${w.apClients} cliente(s)` : "inativo"],
  ]);
  const s = p.sensors;
  dl("sensors", [
    ["Slot 1", s.slot1 ? "Ocupado" : "Livre"], ["Slot 2", s.slot2 ? "Ocupado" : "Livre"],
    ["Leituras NFC", s.nfcReads], ["PN532", s.nfcOk ? "OK" : "sem resposta"], ["Interrupções S1 / S2", s.edges.join(" / ")],
  ]);
  $("prof").innerHTML = p.prof.map((f) => `<tr><td>${esc(f.n)}</td><td class="num">${f.avg}</td><td class="num">${f.min}</td><td class="num">${f.max}</td><td class="num">${f.last}</td><td class="num">${f.calls}</td></tr>`).join("");
  $("tasksTitle").textContent = `Tasks (${p.taskCount})`;
  $("tasks").innerHTML = p.tasks.map((t) => `<tr><td>${esc(t.n)}</td><td class="num">${t.c < 0 ? "ambos" : t.c}</td><td class="num">${t.p}</td><td>${TASK_STATES[t.s] ?? t.s}</td><td class="num">${t.f}</td><td class="num">${t.u}</td></tr>`).join("");
  dl("power", [["Modo", esc(p.power.mode)], ["Modem sleep", p.power.modemSleep ? "ligado" : "desligado"], ["Econômico automático", p.power.autoEco ? "ligado" : "desligado"], ["Último despertar", esc(p.power.wake || "—")]]);

  live.labels.push(fmtUptime(p.uptimeSec));
  live.c0.push(p.cpu[0]);
  live.c1.push(p.cpu[1]);
  live.heap.push(+(p.mem.heapFree / 1024).toFixed(1));
  if (live.labels.length > LIVE_POINTS) ["labels", "c0", "c1", "heap"].forEach((k) => live[k].shift());
  setChart(cCpu, live.labels, live.c0, live.c1);
  setChart(cHeap, live.labels, live.heap);
}

async function loadHistory() {
  const m = await api("/api/history/minutes?step=5");
  const labels = m.map((r) => fmtTime(r[0], r[1] & 1, true));
  setChart(cSens, labels, m.map((r) => r[2]), m.map((r) => r[3]), m.map((r) => r[4]));
  setChart(cCpu24, labels, m.map((r) => r[5]), m.map((r) => r[6]));
  setChart(cNet24, labels, m.map((r) => r[8]), m.map((r) => r[7]));
  setChart(cNfc, labels, m.map((r) => r[9]));
}

async function sleepRequest(mode) {
  const seconds = Number($("sleepSec").value);
  if (mode === "deep" && !confirm("Deep Sleep desliga o Wi-Fi e reinicia o sistema ao acordar. Continuar?")) return;
  try {
    await api("/api/power", { method: "POST", body: { mode, seconds } });
    toast("Entrando em modo de sono. A conexão será perdida.");
  } catch (e) {
    toast(e.status === 401 ? "Faça login na aba Admin" : e.message);
  }
}

function uploadOta() {
  const file = $("otaFile").files[0];
  if (!file) return toast("Escolha o arquivo .bin");
  const form = new FormData();
  form.append("firmware", file, file.name);
  const xhr = new XMLHttpRequest();
  xhr.open("POST", "/api/ota");
  xhr.upload.onprogress = (e) => {
    const pct = Math.round((e.loaded * 100) / e.total);
    $("otaBar").style.width = `${pct}%`;
    $("otaText").textContent = `Enviando: ${pct}%`;
  };
  xhr.onload = () => {
    let body = {};
    try { body = JSON.parse(xhr.responseText); } catch (_) {}
    $("otaText").textContent = xhr.status === 200 ? "Concluído. O armário vai reiniciar." : `Falhou: ${body.error || xhr.status}`;
    if (xhr.status !== 200) $("otaBar").style.width = "0";
  };
  xhr.onerror = () => ($("otaText").textContent = "Conexão perdida durante o envio.");
  xhr.send(form);
}

$("btnLight").onclick = () => sleepRequest("light");
$("btnDeep").onclick = () => sleepRequest("deep");
$("btnOta").onclick = uploadOta;

api("/api/perf").then(render).catch((e) => toast(e.message));
connectSocket((msg) => { if (msg.t === "perf") render(msg); });
loadHistory().catch((e) => toast(e.message));
setInterval(() => loadHistory().catch(() => {}), 300000);
