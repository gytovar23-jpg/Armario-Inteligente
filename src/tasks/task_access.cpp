#include <Arduino.h>
#include <esp_task_wdt.h>

#include "core/access_control.h"
#include "core/shared_state.h"
#include "core/state_machine.h"
#include "core/timebase.h"
#include "core/usage_store.h"
#include "storage/logger.h"
#include "storage/settings.h"
#include "storage/storage.h"
#include "tasks.h"

namespace {
constexpr uint32_t NOTICE_MS = 2500;
constexpr uint32_t SENSOR_RESYNC_MS = 1000;
constexpr uint32_t OVERDUE_CHECK_MS = 60000;

StateMachine fsm;
ActionList actions;
Settings cfg;
uint32_t cfgGeneration = 0;
char sessionName[24];

FsmConfig toFsmConfig(const Settings& s) {
  FsmConfig c;
  c.unlockMs = s.unlockSec * 1000UL;
  c.relockMs = s.relockDelaySec * 1000UL;
  for (uint8_t i = 0; i < SLOT_COUNT; i++) c.maintenance[i] = s.slotMaintenance[i];
  return c;
}

void reloadSettings() {
  if (settings::generation() == cfgGeneration) return;
  cfgGeneration = settings::generation();
  cfg = settings::get();
  fsm.setConfig(toFsmConfig(cfg));
}

void showNotice(const char* text) {
  shared::update([text](LockerSnapshot& s) {
    strlcpy(s.notice, text, sizeof(s.notice));
    s.noticeUntilMs = millis() + NOTICE_MS;
  });
}

void recordEvent(EventType type, uint8_t slot, uint16_t userId, const char* name, const char* uid, uint32_t durationSec) {
  EventRecord ev{};
  ev.ts = timebase::now(&ev.flags);
  ev.durationSec = durationSec;
  ev.userId = userId;
  ev.type = static_cast<uint8_t>(type);
  ev.slot = slot;
  strlcpy(ev.userName, name, sizeof(ev.userName));
  strlcpy(ev.uid, uid, sizeof(ev.uid));
  usage::record(ev);
  storage::postEvent(ev);
}

// A sessão confirmada pode ser anterior ao cartão mais recente (fila de usuários),
// então o nome vem do id gravado na ação, nunca do nome em exibição no OLED.
void resolveUser(uint16_t id, char* name, size_t nameLen, char* uid, size_t uidLen) {
  User user;
  uid[0] = '\0';
  if (id == USER_WEB_ADMIN) {
    strlcpy(name, "Administrador", nameLen);
  } else if (id != USER_NONE && users::getById(id, &user)) {
    strlcpy(name, user.name, nameLen);
    strlcpy(uid, user.uid, uidLen);
  } else {
    strlcpy(name, "Desconhecido", nameLen);
  }
}

void commitWithdraw(const FsmAction& a) {
  ActiveLoan loan{};
  char uid[15];
  loan.active = true;
  loan.userId = a.userId;
  loan.since = timebase::now(&loan.flags);
  resolveUser(a.userId, loan.userName, sizeof(loan.userName), uid, sizeof(uid));

  recordEvent(EventType::Withdraw, a.slot, a.userId, loan.userName, uid, 0);
  storage::postLoan(a.slot, loan);
  shared::update([&](LockerSnapshot& s) {
    s.loans[a.slot] = loan;
    s.overdue[a.slot] = false;
  });
  LOG_INFO("access", "Retirada slot %u por %s", a.slot + 1, loan.userName);
}

uint32_t closeLoan(uint8_t slot) {
  const ActiveLoan loan = shared::read().loans[slot];
  if (!loan.active) return 0;

  uint8_t flags = 0;
  const uint32_t now = timebase::now(&flags);
  // Só há duração real quando início e fim usam o mesmo relógio.
  const bool comparable = (flags & REC_TIME_SYNCED) == (loan.flags & REC_TIME_SYNCED) && now >= loan.since;
  ActiveLoan empty{};
  storage::postLoan(slot, empty);
  shared::update([&](LockerSnapshot& s) {
    s.loans[slot] = empty;
    s.overdue[slot] = false;
  });
  return comparable ? now - loan.since : 0;
}

void commitReturn(const FsmAction& a) {
  const uint32_t duration = closeLoan(a.slot);
  char name[24];
  char uid[15];
  resolveUser(a.userId, name, sizeof(name), uid, sizeof(uid));
  recordEvent(EventType::Return, a.slot, a.userId, name, uid, duration);
  LOG_INFO("access", "Devolucao slot %u por %s (%lu s)", a.slot + 1, name, (unsigned long)duration);
}

void noteViolation(const FsmAction& a) {
  if (a.occupied) closeLoan(a.slot);  // Notebook voltou sem sessão: o empréstimo acabou.
  recordEvent(EventType::Violation, a.slot, USER_NONE, "", "", 0);
  LOG_ERROR("access", "Violacao: slot %u mudou com o armario trancado", a.slot + 1);
}

void handleNote(const FsmAction& a) {
  switch (a.note) {
    case FsmNote::SessionStarted:
      LOG_INFO("access", "Sessao iniciada: %s", sessionName);
      break;
    case FsmNote::SessionEnded:
      LOG_INFO("access", "Sessao encerrada; trancado");
      break;
    case FsmNote::EnrollTimedOut:
      LOG_INFO("access", "Cadastro expirou sem cartao");
      break;
    case FsmNote::SlotInMaintenance:
      LOG_INFO("access", "Slot %u em manutencao mudou de estado", a.slot + 1);
      break;
    case FsmNote::Refused:
      LOG_WARN("access", "Comando recusado no estado atual");
      break;
    case FsmNote::None:
      break;
  }
}

void execute() {
  for (uint8_t i = 0; i < actions.count; i++) {
    const FsmAction& a = actions.items[i];
    switch (a.type) {
      case ActionType::EngageLock:
        shared::sendActuator(ActuatorCmd::Lock);
        break;
      case ActionType::ReleaseLock:
        shared::sendActuator(ActuatorCmd::Unlock);
        break;
      case ActionType::FeedbackGranted:
        shared::sendActuator(ActuatorCmd::Feedback, uint8_t(FeedbackKind::Granted));
        break;
      case ActionType::FeedbackDenied:
        shared::sendActuator(ActuatorCmd::Feedback, uint8_t(FeedbackKind::Denied));
        break;
      case ActionType::FeedbackEnrollDone:
        shared::sendActuator(ActuatorCmd::Feedback, uint8_t(FeedbackKind::EnrollDone));
        break;
      case ActionType::AlarmOn:
        shared::sendActuator(ActuatorCmd::Feedback, uint8_t(FeedbackKind::AlarmOn));
        break;
      case ActionType::AlarmOff:
        shared::sendActuator(ActuatorCmd::Feedback, uint8_t(FeedbackKind::AlarmOff));
        break;
      case ActionType::LightOn:
        shared::sendActuator(ActuatorCmd::Aux, 1);
        break;
      case ActionType::LightOff:
        shared::sendActuator(ActuatorCmd::Aux, 0);
        break;
      case ActionType::CommitWithdraw:
        commitWithdraw(a);
        break;
      case ActionType::CommitReturn:
        commitReturn(a);
        break;
      case ActionType::SlotViolation:
        noteViolation(a);
        break;
      case ActionType::Note:
        handleNote(a);
        break;
    }
  }
  actions.clear();
}

void publish() {
  const LockerState state = fsm.state();
  shared::lock();
  LockerSnapshot& s = shared::raw();
  const bool changed = s.state != state || s.alarm != fsm.alarmReason() || s.alarmSilenced != fsm.alarmSilenced() ||
                       s.locked != fsm.locked() ||
                       s.slotOccupied[0] != fsm.occupied(0) || s.slotOccupied[1] != fsm.occupied(1);
  s.state = state;
  s.alarm = fsm.alarmReason();
  s.alarmSilenced = fsm.alarmSilenced();
  s.locked = fsm.locked();
  for (uint8_t i = 0; i < SLOT_COUNT; i++) s.slotOccupied[i] = fsm.occupied(i);
  strlcpy(s.sessionUser, state == LockerState::Idle ? "" : sessionName, sizeof(s.sessionUser));
  shared::unlock();
  if (changed) shared::stateVersion++;
}

void step(const FsmEvent& ev) {
  fsm.handle(ev, actions);
  execute();
  publish();
}

void onCard(const UidMsg& card) {
  char hex[15];
  accessctl::uidToHex(card.uid, card.len, hex, sizeof(hex));
  const uint32_t now = millis();

  if (fsm.state() == LockerState::Enroll) {
    step({FsmInput::EnrollCaptured, now});
    shared::update([&](LockerSnapshot& s) {
      strlcpy(s.lastUid, hex, sizeof(s.lastUid));
      s.enrollCaptureId++;
    });
    showNotice("Cartao capturado");
    LOG_INFO("access", "Cartao %s capturado para cadastro", hex);
    return;
  }

  User user;
  const AccessDecision decision = accessctl::decide(hex, cfg, &user);
  if (decision == AccessDecision::Granted) {
    strlcpy(sessionName, user.name, sizeof(sessionName));
    FsmEvent ev{FsmInput::CardGranted, now};
    ev.userId = user.id;
    ev.admin = user.admin;
    step(ev);
    LOG_INFO("access", "Acesso liberado: %s", user.name);
    return;
  }

  recordEvent(EventType::Denied, 0xFF, USER_NONE, decision == AccessDecision::UnknownCard ? "" : user.name, hex, 0);
  showNotice("Acesso negado");
  step({FsmInput::CardDenied, now});
  LOG_WARN("access", "Acesso negado: %s (%s)", hex, accessctl::describe(decision));
}

void onSensor(const SensorMsg& msg) {
  SensorChange change;
  bool changed;
  sensors::process(msg, &change, &changed);
  if (!changed) return;

  FsmEvent ev{change.active ? FsmInput::SlotOccupied : FsmInput::SlotFreed, millis()};
  ev.slot = change.id == SensorId::Slot1 ? 0 : 1;
  step(ev);
}

void onCommand(const AccessCommand& cmd) {
  FsmEvent ev{FsmInput::Tick, millis()};
  ev.userId = USER_WEB_ADMIN;
  switch (cmd.kind) {
    case AccessCommand::Unlock:
      strlcpy(sessionName, "Administrador", sizeof(sessionName));
      ev.type = FsmInput::AdminUnlock;
      LOG_INFO("access", "Destrancamento manual pela web");
      break;
    case AccessCommand::Lock:
      ev.type = FsmInput::AdminLock;
      break;
    case AccessCommand::Silence:
      ev.type = FsmInput::SilenceAlarm;
      LOG_INFO("access", "Alarme silenciado pela web");
      break;
    case AccessCommand::EnrollStart:
      ev.type = FsmInput::EnrollStart;
      break;
    case AccessCommand::EnrollCancel:
      ev.type = FsmInput::EnrollCancel;
      break;
  }
  step(ev);
}

void checkOverdueLoans() {
  if (!timebase::synced()) return;
  const uint32_t limit = cfg.maxLoanHours * 3600UL;
  uint8_t flags = 0;
  const uint32_t now = timebase::now(&flags);
  const LockerSnapshot snap = shared::read();
  for (uint8_t i = 0; i < SLOT_COUNT; i++) {
    const ActiveLoan& loan = snap.loans[i];
    const bool late = loan.active && (loan.flags & REC_TIME_SYNCED) && now - loan.since > limit;
    if (late && !snap.overdue[i]) {
      LOG_WARN("access", "Emprestimo vencido no slot %u: %s", i + 1, loan.userName);
      shared::update([i](LockerSnapshot& s) { s.overdue[i] = true; });
    }
  }
}

void restoreLoans() {
  bool occupied[SLOT_COUNT] = {sensors::active(SensorId::Slot1), sensors::active(SensorId::Slot2)};
  for (uint8_t i = 0; i < SLOT_COUNT; i++) {
    ActiveLoan loan{};
    if (!storage::loadLoan(i, &loan) || !loan.active) continue;
    if (occupied[i]) {
      // Devolvido com o sistema desligado: não há como saber por quem nem quando.
      LOG_WARN("access", "Slot %u ocupado com emprestimo aberto; encerrando", i + 1);
      storage::postLoan(i, ActiveLoan{});
      continue;
    }
    shared::update([&](LockerSnapshot& s) { s.loans[i] = loan; });
  }
}
}  // namespace

void taskAccess(void*) {
  tasks::watchThisTask();

  if (!sensors::begin(shared::sensorQueue)) LOG_ERROR("access", "Falha ao iniciar sensores");
  reloadSettings();
  restoreLoans();

  const bool occupied[SLOT_COUNT] = {sensors::active(SensorId::Slot1), sensors::active(SensorId::Slot2)};
  fsm.begin(millis(), occupied, actions);
  execute();
  publish();

  uint32_t lastResync = millis();
  uint32_t lastOverdue = millis();

  for (;;) {
    // Timeout = passo do relógio da FSM. 50 ms dão precisão de sobra aos prazos,
    // que são de segundos, sem acordar à toa.
    QueueSetMemberHandle_t ready = xQueueSelectFromSet(shared::accessSet, pdMS_TO_TICKS(50));
    if (ready == shared::sensorQueue) {
      SensorMsg msg;
      if (xQueueReceive(shared::sensorQueue, &msg, 0) == pdTRUE) onSensor(msg);
    } else if (ready == shared::uidQueue) {
      UidMsg msg;
      if (xQueueReceive(shared::uidQueue, &msg, 0) == pdTRUE) onCard(msg);
    } else if (ready == shared::commandQueue) {
      AccessCommand msg;
      if (xQueueReceive(shared::commandQueue, &msg, 0) == pdTRUE) onCommand(msg);
    }

    reloadSettings();
    const uint32_t now = millis();
    step({FsmInput::Tick, now});

    if (now - lastResync >= SENSOR_RESYNC_MS) {
      lastResync = now;
      sensors::resync();
    }
    if (now - lastOverdue >= OVERDUE_CHECK_MS) {
      lastOverdue = now;
      checkOverdueLoans();
    }
    esp_task_wdt_reset();
  }
}
