const show = (el, on) => el.classList.toggle("hide", !on);
let usersCache = [];
let currentState = null;
let enrollPoll = null;

// ---- Sessão -------------------------------------------------------------------
async function checkSession() {
  const s = await api("/api/session");
  show($("loginBox"), !s.authenticated);
  show($("passwordBox"), s.authenticated && s.mustChange);
  const ready = s.authenticated && !s.mustChange;
  show($("app"), ready);
  if (ready) startApp();
}

$("loginForm").onsubmit = async (ev) => {
  ev.preventDefault();
  const f = ev.target;
  try {
    await api("/api/login", { method: "POST", body: { user: f.user.value, password: f.password.value } });
    f.password.value = "";
    checkSession();
  } catch (e) { toast(e.message); }
};

async function changePassword(ev) {
  ev.preventDefault();
  const f = ev.target;
  try {
    await api("/api/password", { method: "POST", body: { current: f.current.value, next: f.next.value } });
    f.reset();
    toast("Senha alterada");
    checkSession();
  } catch (e) { toast(e.message); }
}
$("passwordForm").onsubmit = changePassword;
$("passwordForm2").onsubmit = changePassword;

$("logoutBtn").onclick = async () => { await api("/api/logout", { method: "POST" }); location.reload(); };

// ---- Abas ---------------------------------------------------------------------
const sections = [...document.querySelectorAll("[data-tab]")];
function openTab(name) {
  sections.forEach((s) => show(s, s.dataset.tab === name));
  [...$("tabs").children].forEach((b) => b.classList.toggle("on", b.textContent === name));
  ({ "Usuários": loadUsers, "Empréstimos": loadLoans, "Logs": loadLogs, "Configurações": loadSettings, "Slots": loadSlots })[name]?.()?.catch?.((e) => toast(e.message));
}
$("tabs").innerHTML = sections.map((s) => `<button>${s.dataset.tab}</button>`).join("");
[...$("tabs").children].forEach((b) => (b.onclick = () => openTab(b.textContent)));

// ---- Usuários -----------------------------------------------------------------
async function loadUsers() {
  usersCache = await api("/api/users");
  renderUsers();
  const select = document.querySelector("#historyForm [name=user]");
  select.innerHTML = `<option value="">Todos</option>` + usersCache.map((u) => `<option value="${u.id}">${esc(u.name)}</option>`).join("");
}

function renderUsers() {
  const q = $("userSearch").value.toLowerCase();
  const list = usersCache.filter((u) => `${u.name} ${u.ra} ${u.uid}`.toLowerCase().includes(q));
  $("userRows").innerHTML = list.map((u) => `<tr>
    <td>${esc(u.name)}</td><td>${esc(u.ra)}</td><td>${esc(u.email)}</td><td>${esc(u.uid) || "—"}</td>
    <td><span class="tag ${u.active ? "ok" : "bad"}">${u.active ? "Ativo" : "Bloqueado"}</span></td>
    <td>${u.admin ? "Admin" : "Comum"}</td>
    <td><button data-edit="${u.id}">Editar</button> <button data-del="${u.id}" class="danger">Excluir</button></td></tr>`).join("") || `<tr><td class="muted" colspan="7">Nenhum usuário</td></tr>`;
}

$("userSearch").oninput = renderUsers;

$("userRows").onclick = async (ev) => {
  const { edit, del } = ev.target.dataset;
  if (edit) {
    const u = usersCache.find((x) => x.id == edit);
    const f = $("userForm");
    ["id", "name", "ra", "email", "uid"].forEach((k) => (f[k].value = u[k]));
    f.active.checked = u.active;
    f.admin.checked = u.admin;
    $("userTitle").textContent = `Editando ${u.name}`;
    f.scrollIntoView();
  }
  if (del && confirm("Excluir este usuário?")) {
    try { await api(`/api/users?id=${del}`, { method: "DELETE" }); loadUsers(); } catch (e) { toast(e.message); }
  }
};

function resetUserForm() {
  $("userForm").reset();
  $("userForm").id.value = "";
  $("userTitle").textContent = "Novo usuário";
}
$("userReset").onclick = resetUserForm;

$("userForm").onsubmit = async (ev) => {
  ev.preventDefault();
  const f = ev.target;
  const body = { name: f.name.value, ra: f.ra.value, email: f.email.value, uid: f.uid.value, active: f.active.checked, admin: f.admin.checked };
  if (f.id.value) body.id = Number(f.id.value);
  try {
    await api("/api/users", { method: "POST", body });
    resetUserForm();
    toast("Usuário salvo");
    loadUsers();
  } catch (e) { toast(e.message); }
};

// O cartão é lido pelo core físico; aqui só acompanhamos a captura.
$("enrollBtn").onclick = async () => {
  if (enrollPoll) return;
  const before = await api("/api/enroll");
  await api("/api/enroll", { method: "POST", body: { action: "start" } });
  $("enrollText").textContent = "Aproxime o cartão do leitor (30 s)...";
  const started = Date.now();
  enrollPoll = setInterval(async () => {
    const s = await api("/api/enroll").catch(() => null);
    if (!s) return;
    if (s.captureId !== before.captureId) {
      $("userForm").uid.value = s.uid;
      $("enrollText").textContent = `Cartão capturado: ${s.uid}`;
    } else if (Date.now() - started < 31000) {
      return;
    } else {
      $("enrollText").textContent = "Tempo esgotado.";
    }
    clearInterval(enrollPoll);
    enrollPoll = null;
  }, 700);
};

// ---- Slots --------------------------------------------------------------------
async function loadSlots() {
  const s = await api("/api/settings");
  $("slotForm").innerHTML = s.slots.map((slot, i) => `
    <label>Patrimônio do slot ${i + 1}<input name="asset${i}" maxlength="23" value="${esc(slot.asset)}"></label>
    <label class="check"><input name="maint${i}" type="checkbox" ${slot.maintenance ? "checked" : ""}>Em manutenção</label>`).join("") + `<button class="primary">Salvar</button>`;
}

$("slotForm").onsubmit = async (ev) => {
  ev.preventDefault();
  const f = ev.target;
  const slots = [0, 1].map((i) => ({ asset: f[`asset${i}`].value, maintenance: f[`maint${i}`].checked }));
  try { await api("/api/settings", { method: "POST", body: { slots } }); toast("Slots salvos"); } catch (e) { toast(e.message); }
};

// ---- Empréstimos ---------------------------------------------------------------
function historyQuery(form) {
  const p = new URLSearchParams();
  if (form.from.value) p.set("from", new Date(`${form.from.value}T00:00:00`).getTime() / 1000);
  if (form.to.value) p.set("to", new Date(`${form.to.value}T23:59:59`).getTime() / 1000);
  if (form.user.value) p.set("user", form.user.value);
  if (form.slot.value) p.set("slot", form.slot.value);
  return p;
}

async function loadLoans() {
  if (!usersCache.length) await loadUsers();
  const s = await api("/api/state");
  const active = s.slots.filter((x) => x.loan);
  $("loanRows").innerHTML = active.map((x) => `<tr><td>${x.id}${x.asset ? ` · ${esc(x.asset)}` : ""}</td><td>${esc(x.loan.user)}</td><td>${fmtTime(x.loan.since, x.loan.synced)}</td><td><span class="tag ${x.overdue ? "bad" : "ok"}">${x.overdue ? "Vencido" : "No prazo"}</span></td></tr>`).join("") || `<tr><td class="muted" colspan="4">Nenhum empréstimo ativo</td></tr>`;
  loadHistory();
}

async function loadHistory() {
  const rows = await api(`/api/history/events?${historyQuery($("historyForm"))}`);
  $("historyRows").innerHTML = rows.map((e) => `<tr><td>${fmtTime(e.ts, e.synced)}</td><td>${esc(e.type)}</td><td>${e.slot || "—"}</td><td>${esc(e.user || "—")}</td><td>${esc(e.uid || "—")}</td><td>${fmtDuration(e.dur)}</td></tr>`).join("") || `<tr><td class="muted" colspan="6">Sem registros</td></tr>`;
}
$("historyForm").onsubmit = (ev) => { ev.preventDefault(); loadHistory().catch((e) => toast(e.message)); };
$("historyCsv").onclick = () => {
  const q = historyQuery($("historyForm"));
  q.set("format", "csv");
  location.href = `/api/history/events?${q}`;
};

// ---- Controle e diagnóstico ----------------------------------------------------
document.querySelector("[data-control]").onclick = async (ev) => {
  const action = ev.target.dataset.action;
  if (!action) return;
  try { await api("/api/control", { method: "POST", body: { action } }); toast("Comando enviado"); } catch (e) { toast(e.message); }
};

const DIAG = [["servo", "Servo"], ["buzzer", "Buzzer"], ["green", "LED verde"], ["red", "LED vermelho"], ["aux", "Atuador 2 liga"], ["auxoff", "Atuador 2 desliga"], ["nfc", "NFC"], ["oled", "OLED"], ["sensors", "Sensores"], ["flash", "Flash"]];
$("diag").innerHTML = DIAG.map(([k, label]) => `<button data-t="${k}">${label}</button>`).join("");
$("diag").onclick = async (ev) => {
  const t = ev.target.dataset.t;
  if (!t) return;
  const body = t.startsWith("aux") ? { target: "aux", on: t === "aux" } : { target: t };
  try { $("diagOut").textContent = JSON.stringify(await api("/api/diag", { method: "POST", body }), null, 2); } catch (e) { toast(e.message); }
};

// ---- Configurações -------------------------------------------------------------
const FIELDS = {
  fFreq: [["sensorSampleMs", "Amostragem dos sensores (ms)", "number"], ["nfcPollMs", "Leitura NFC (ms)", "number"]],
  fOp: [["unlockSec", "Tempo destrancado após o cartão (s)", "number"], ["relockDelaySec", "Trancar após mexer num slot (s)", "number"],
    ["servoLockedDeg", "Ângulo trancado", "number"], ["servoUnlockedDeg", "Ângulo destrancado", "number"], ["beepVolume", "Volume do bipe (%)", "number"],
    ["beepPattern", "Duração do bipe (0 curto, 1 normal, 2 longo)", "number"], ["hoursEnforced", "Aplicar horário de funcionamento", "checkbox"],
    ["openHour", "Abre às (h)", "number"], ["closeHour", "Fecha às (h)", "number"], ["maxLoanHours", "Prazo máximo do empréstimo (h)", "number"], ["eventRetentionDays", "Retenção de eventos (dias)", "number"]],
  fWifi: [["wifiSsid", "Rede (SSID)", "text"], ["wifiPass", "Senha", "password"], ["staticIp", "IP estático", "checkbox"], ["ip", "IP", "text"], ["gateway", "Gateway", "text"], ["subnet", "Máscara", "text"], ["dns", "DNS", "text"]],
  fTime: [["timezone", "Fuso (POSIX TZ)", "text"], ["ntpServer", "Servidor NTP", "text"]],
  fPower: [["cpuMhz", "Frequência da CPU", "select"], ["modemSleep", "Modem sleep", "checkbox"], ["autoEco", "Econômico fora do horário", "checkbox"], ["publicPages", "Dashboard e Performance públicas", "checkbox"]],
};

function fieldHtml([name, label, type]) {
  if (type === "checkbox") return `<label class="check"><input name="${name}" type="checkbox">${label}</label>`;
  if (type === "select") return `<label>${label}<select name="${name}"><option>240</option><option>160</option><option>80</option></select></label>`;
  return `<label>${label}<input name="${name}" type="${type}"></label>`;
}
Object.entries(FIELDS).forEach(([box, list]) => ($(box).innerHTML = list.map(fieldHtml).join("")));

async function loadSettings() {
  const s = await api("/api/settings");
  const form = $("settingsForm");
  Object.values(FIELDS).flat().forEach(([name, , type]) => {
    if (name === "wifiPass") return;
    if (type === "checkbox") form[name].checked = s[name];
    else form[name].value = s[name];
  });
}

$("settingsForm").onsubmit = async (ev) => {
  ev.preventDefault();
  const form = ev.target;
  const body = {};
  Object.values(FIELDS).flat().forEach(([name, , type]) => {
    if (type === "checkbox") body[name] = form[name].checked;
    else if (type === "number" || type === "select") body[name] = Number(form[name].value);
    else if (name !== "wifiPass" || form[name].value) body[name] = form[name].value;
  });
  try { await api("/api/settings", { method: "POST", body }); form.wifiPass.value = ""; toast("Configurações aplicadas"); } catch (e) { toast(e.message); }
};

$("scanBtn").onclick = async () => {
  await api("/api/wifi/scan", { method: "POST" });
  toast("Buscando redes...");
  for (let i = 0; i < 15; i++) {
    await new Promise((r) => setTimeout(r, 1000));
    const res = await api("/api/wifi/scan");
    if (!res.done) continue;
    const list = $("scanList");
    list.innerHTML = `<option value="">Escolha uma rede</option>` + res.networks.map((n) => `<option value="${esc(n.ssid)}">${esc(n.ssid)} (${n.rssi} dBm${n.open ? ", aberta" : ""})</option>`).join("");
    show(list, true);
    return;
  }
  toast("A busca não terminou");
};
$("scanList").onchange = (ev) => { if (ev.target.value) $("settingsForm").wifiSsid.value = ev.target.value; };

// ---- Logs ----------------------------------------------------------------------
async function loadLogs() {
  const level = $("logLevel").value;
  const rows = await api(`/api/logs?limit=200${level ? `&level=${level}` : ""}`);
  const tag = { ERROR: "bad", WARN: "warn", INFO: "ok" };
  $("logRows").innerHTML = rows.map((r) => `<tr><td>${esc(r.when)}</td><td><span class="tag ${tag[r.level]}">${r.level}</span></td><td>${esc(r.module)}</td><td class="wrap">${esc(r.msg)}</td></tr>`).join("") || `<tr><td class="muted" colspan="4">Sem registros</td></tr>`;
}
$("logLevel").onchange = loadLogs;
$("logRefresh").onclick = loadLogs;
$("logClear").onclick = async () => {
  if (!confirm("Apagar todos os logs?")) return;
  await api("/api/logs", { method: "DELETE" });
  setTimeout(loadLogs, 400);
};

// ---- Sistema -------------------------------------------------------------------
$("rebootBtn").onclick = async () => {
  if (!confirm("Reiniciar o armário?")) return;
  await api("/api/reboot", { method: "POST" });
  toast("Reiniciando...");
};

$("restoreBtn").onclick = async () => {
  const file = $("restoreFile").files[0];
  if (!file) return toast("Escolha o arquivo de backup");
  try {
    const body = JSON.parse(await file.text());
    await api("/api/restore", { method: "POST", body });
    toast("Backup restaurado");
  } catch (e) { toast(e.message); }
};

let started = false;
function startApp() {
  if (started) return;
  started = true;
  openTab("Usuários");
  connectSocket((msg) => {
    if (msg.t !== "state") return;
    currentState = msg;
    $("controlState").textContent = `Estado: ${msg.state}. Tranca ${msg.locked ? "fechada" : "aberta"}.`;
  });
}

checkSession().catch((e) => toast(e.message));
