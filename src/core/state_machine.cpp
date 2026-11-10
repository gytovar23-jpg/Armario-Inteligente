#include "state_machine.h"

namespace {
// Comparação por diferença: continua correta quando millis() dá a volta (49 dias).
bool reached(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }
}  // namespace

void StateMachine::note(FsmNote n, ActionList& out) {
  FsmAction a{ActionType::Note};
  a.note = n;
  a.userId = user_;
  out.push(a);
}

void StateMachine::engage(ActionList& out) {
  locked_ = true;
  out.push({ActionType::EngageLock});
}

void StateMachine::begin(uint32_t nowMs, const bool occupied[SLOT_COUNT], ActionList& out) {
  now_ = nowMs;
  for (uint8_t i = 0; i < SLOT_COUNT; i++) {
    occupied_[i] = occupied[i];
    initial_[i] = occupied[i];
  }
  // Sem sensor de porta não há sessão a retomar depois de um reset: começa trancado.
  engage(out);
  state_ = LockerState::Idle;
}

void StateMachine::handle(const FsmEvent& ev, ActionList& out) {
  now_ = ev.nowMs;
  switch (ev.type) {
    case FsmInput::Tick:
      onTick(out);
      break;
    case FsmInput::CardGranted:
      onCardGranted(ev, out);
      break;
    case FsmInput::CardDenied:
      onCardDenied(out);
      break;
    case FsmInput::SlotOccupied:
      onSlot(ev.slot, true, out);
      break;
    case FsmInput::SlotFreed:
      onSlot(ev.slot, false, out);
      break;
    case FsmInput::AdminUnlock:
      onAdminUnlock(ev, out);
      break;
    case FsmInput::AdminLock:
      onAdminLock(out);
      break;
    case FsmInput::SilenceAlarm:
      onSilence(out);
      break;
    case FsmInput::EnrollStart:
      if (state_ == LockerState::Idle) {
        state_ = LockerState::Enroll;
        deadline_ = now_ + cfg_.enrollMs;
      } else {
        note(FsmNote::Refused, out);
      }
      break;
    case FsmInput::EnrollCancel:
      if (state_ == LockerState::Enroll) state_ = LockerState::Idle;
      break;
    case FsmInput::EnrollCaptured:
      if (state_ == LockerState::Enroll) {
        state_ = LockerState::Idle;
        out.push({ActionType::FeedbackEnrollDone});
      }
      break;
  }
}

void StateMachine::onTick(ActionList& out) {
  if (!reached(now_, deadline_)) return;
  switch (state_) {
    case LockerState::Open:
      finishSession(out);
      break;
    case LockerState::Enroll:
      state_ = LockerState::Idle;
      note(FsmNote::EnrollTimedOut, out);
      break;
    default:
      break;
  }
}

void StateMachine::onSlot(uint8_t slot, bool occupied, ActionList& out) {
  if (slot >= SLOT_COUNT || occupied_[slot] == occupied) return;
  occupied_[slot] = occupied;

  if (cfg_.maintenance[slot]) {
    FsmAction a{ActionType::Note};
    a.slot = slot;
    a.note = FsmNote::SlotInMaintenance;
    out.push(a);
    return;
  }
  if (state_ == LockerState::Open) {
    deadline_ = now_ + cfg_.relockMs;
    return;
  }

  FsmAction a{ActionType::SlotViolation};
  a.slot = slot;
  a.occupied = occupied;
  out.push(a);
  if (state_ != LockerState::Alarm) enterAlarm(AlarmReason::Violation, out);
}

void StateMachine::onCardGranted(const FsmEvent& ev, ActionList& out) {
  switch (state_) {
    case LockerState::Idle:
    case LockerState::Open:
      out.push({ActionType::FeedbackGranted});
      openFor(ev.userId, out);
      break;
    case LockerState::Alarm:
      if (ev.admin) {
        onSilence(out);
      } else {
        out.push({ActionType::FeedbackDenied});
      }
      break;
    case LockerState::Enroll:
      break;
  }
}

void StateMachine::onCardDenied(ActionList& out) {
  if (state_ != LockerState::Enroll) out.push({ActionType::FeedbackDenied});
}

void StateMachine::onSilence(ActionList& out) {
  if (state_ != LockerState::Alarm) return;
  out.push({ActionType::AlarmOff});
  leaveAlarmToIdle(out);
}

void StateMachine::onAdminUnlock(const FsmEvent& ev, ActionList& out) {
  if (state_ == LockerState::Idle || state_ == LockerState::Open) {
    openFor(ev.userId, out);
  } else {
    note(FsmNote::Refused, out);
  }
}

void StateMachine::onAdminLock(ActionList& out) {
  if (state_ == LockerState::Open) {
    finishSession(out);
  } else {
    note(FsmNote::Refused, out);
  }
}

void StateMachine::openFor(uint16_t user, ActionList& out) {
  if (state_ != LockerState::Open) {
    startSession(user, out);
    return;
  }
  if (user != user_) {
    // Fila de usuários: fecha o saldo do anterior antes de abrir o do próximo.
    commitSession(out);
    user_ = user;
    note(FsmNote::SessionStarted, out);
  }
  deadline_ = now_ + cfg_.unlockMs;
}

void StateMachine::startSession(uint16_t user, ActionList& out) {
  user_ = user;
  state_ = LockerState::Open;
  deadline_ = now_ + cfg_.unlockMs;
  locked_ = false;
  for (uint8_t i = 0; i < SLOT_COUNT; i++) initial_[i] = occupied_[i];
  out.push({ActionType::ReleaseLock});
  note(FsmNote::SessionStarted, out);
  out.push({ActionType::LightOn});
}

void StateMachine::commitSession(ActionList& out) {
  // Grava o saldo da sessão, não cada transição: tirar e repor o mesmo notebook
  // na mesma abertura não é empréstimo.
  for (uint8_t i = 0; i < SLOT_COUNT; i++) {
    if (occupied_[i] == initial_[i] || cfg_.maintenance[i]) continue;
    FsmAction a{occupied_[i] ? ActionType::CommitReturn : ActionType::CommitWithdraw};
    a.slot = i;
    a.userId = user_;
    a.occupied = occupied_[i];
    out.push(a);
    initial_[i] = occupied_[i];
  }
}

void StateMachine::finishSession(ActionList& out) {
  commitSession(out);
  engage(out);
  out.push({ActionType::LightOff});
  note(FsmNote::SessionEnded, out);
  state_ = LockerState::Idle;
  user_ = USER_NONE;
}

void StateMachine::enterAlarm(AlarmReason reason, ActionList& out) {
  state_ = LockerState::Alarm;
  alarm_ = reason;
  out.push({ActionType::AlarmOn});
}

void StateMachine::leaveAlarmToIdle(ActionList& out) {
  engage(out);
  state_ = LockerState::Idle;
  alarm_ = AlarmReason::None;
  user_ = USER_NONE;
}
