// Tela simples para quem usa o armário: quem está com cada notebook e o que já voltou.
const BANNER = {
  OPEN: ["ok", "Armário destrancado. A retirada ou devolução é registrada quando ele trancar."],
  ALARM: ["bad", "Alarme! Um slot mudou com o armário trancado."],
  ENROLL: ["warn", "Cadastro de cartão em andamento."],
};

let last = null;

const notebookName = (slotId) => {
  const slot = last?.slots.find((s) => s.id === slotId);
  return slot?.asset ? `${esc(slot.asset)} <span class="muted">(slot ${slotId})</span>` : `Slot ${slotId}`;
};

function renderState(s) {
  last = s;
  const [kind, text] = BANNER[s.state] || ["ok", s.locked ? "Armário trancado. Aproxime o cartão para retirar ou devolver." : "Armário destrancado."];
  $("banner").className = `banner ${kind}`;
  $("banner").textContent = text;
  $("slots").innerHTML = s.slots.map((slot) => {
    let body;
    if (slot.maintenance) body = `<span class="tag warn">Em manutenção</span>`;
    else if (slot.occupied) body = `<span class="tag ok">Disponível</span>`;
    else if (slot.loan) body = `<span class="tag ${slot.overdue ? "bad" : "warn"}">${slot.overdue ? "Em uso, prazo vencido" : "Em uso"}</span>
      <p class="who">${esc(slot.loan.user)}</p><p class="muted">desde ${fmtTime(slot.loan.since, slot.loan.synced)}</p>`;
    else body = `<span class="tag warn">Fora do slot</span>`;
    return `<section class="card slot"><h2>${notebookName(slot.id)}</h2>${body}</section>`;
  }).join("");
}

// O firmware grava a retirada e a devolução como eventos separados. Lendo do mais
// novo para o mais antigo, cada devolução fica pendente até achar a retirada do mesmo slot.
// Só a retirada mais recente de um slot pode estar em uso; as antigas sem par foram
// encerradas por violação (notebook reposto sem cartão).
function pairLoans(events) {
  const rows = [];
  const waiting = {};
  const seen = {};
  for (const e of events) {
    if (e.type !== "retirada" && e.type !== "devolucao") continue;
    const latest = !seen[e.slot];
    seen[e.slot] = true;
    if (e.type === "devolucao") {
      const row = { user: e.user, slot: e.slot, back: e, out: e.dur ? { ts: e.ts - e.dur, synced: e.synced } : null };
      waiting[e.slot] = row;
      rows.push(row);
    } else {
      const row = waiting[e.slot];
      if (row) {
        row.out = e;
        row.user = e.user || row.user;
        delete waiting[e.slot];
      } else {
        rows.push({ user: e.user, slot: e.slot, out: e, back: null, latest });
      }
    }
  }
  return rows;
}

function renderLoans(events) {
  const rows = pairLoans(events);
  $("loans").innerHTML = rows.map((r) => {
    const open = r.latest && last?.slots.find((s) => s.id === r.slot)?.loan;
    const status = r.back ? `<span class="tag ok">Devolvido</span>`
      : open ? `<span class="tag warn">Em uso</span>`
      : `<span class="tag">Encerrado</span>`;
    return `<tr><td>${esc(r.user || "—")}</td><td>${notebookName(r.slot)}</td>
      <td>${r.out ? fmtTime(r.out.ts, r.out.synced) : "—"}</td><td>${r.back ? fmtTime(r.back.ts, r.back.synced) : "—"}</td><td>${status}</td></tr>`;
  }).join("") || `<tr><td class="muted" colspan="5">Nenhuma retirada registrada</td></tr>`;
}

const loadLoans = () => api("/api/events?limit=60").then(renderLoans);

function refresh() {
  api("/api/state").then(renderState).then(loadLoans).catch((e) => toast(e.message));
}

// O evento vai para a flash por uma fila; esperar um pouco evita ler antes de gravar.
let reloadTimer;
connectSocket((msg) => {
  if (msg.t !== "state") return;
  renderState(msg);
  clearTimeout(reloadTimer);
  reloadTimer = setTimeout(() => loadLoans().catch(() => {}), 1000);
});
refresh();
setInterval(refresh, 60000);
