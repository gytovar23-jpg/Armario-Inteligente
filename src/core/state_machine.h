#pragma once
// Máquina de estados do armário. É pura: recebe eventos e o tempo como dados e
// devolve ações, sem tocar em hardware. Por isso roda nos testes do PC, e todo
// caminho de alarme pode ser exercitado sem montar a bancada.
//
// O armário não tem sensor de porta. A sessão é uma janela de tempo aberta pelo
// cartão: a tranca solta, os slots podem mudar, e a tranca volta quando a janela
// acaba. Cada mudança de slot encurta ou estende a janela para `relockMs`, então
// quem já retirou ou devolveu não espera o prazo inteiro.
#include <stdint.h>

#include "types.h"

enum class LockerState : uint8_t { Idle, Open, Alarm, Enroll };
enum class AlarmReason : uint8_t { None, Violation };

enum class FsmInput : uint8_t {
  Tick,
  CardGranted,
  CardDenied,
  SlotOccupied,
  SlotFreed,
  AdminUnlock,
  AdminLock,
  SilenceAlarm,
  EnrollStart,
  EnrollCancel,
  EnrollCaptured,
};

struct FsmEvent {
  FsmInput type;
  uint32_t nowMs;
  uint8_t slot = 0;
  uint16_t userId = USER_NONE;
  bool admin = false;
};

enum class ActionType : uint8_t {
  EngageLock,
  ReleaseLock,
  FeedbackGranted,
  FeedbackDenied,
  FeedbackEnrollDone,
  AlarmOn,
  AlarmOff,
  LightOn,
  LightOff,
  CommitWithdraw,   // Retirada confirmada ao fim da sessão.
  CommitReturn,     // Devolução confirmada ao fim da sessão.
  SlotViolation,    // Slot mudou sem sessão.
  Note,             // Só registro; `note` diz o quê.
};

enum class FsmNote : uint8_t {
  None,
  SessionStarted,
  SessionEnded,
  EnrollTimedOut,
  SlotInMaintenance,
  Refused,  // Comando recebido em estado que não o aceita.
};

struct FsmAction {
  ActionType type;
  uint8_t slot = 0;
  uint16_t userId = USER_NONE;
  bool occupied = false;
  FsmNote note = FsmNote::None;
};

struct ActionList {
  static constexpr uint8_t CAPACITY = 10;
  FsmAction items[CAPACITY];
  uint8_t count = 0;

  void push(const FsmAction& a) {
    if (count < CAPACITY) items[count++] = a;
  }
  void clear() { count = 0; }
};

struct FsmConfig {
  uint32_t unlockMs = 15000;  // Duração da sessão aberta pelo cartão.
  uint32_t relockMs = 3000;   // Prazo restante depois de cada mudança de slot.
  uint32_t enrollMs = ENROLL_TIMEOUT_MS;
  bool maintenance[SLOT_COUNT] = {};
};

class StateMachine {
 public:
  void setConfig(const FsmConfig& config) { cfg_ = config; }
  void begin(uint32_t nowMs, const bool occupied[SLOT_COUNT], ActionList& out);
  void handle(const FsmEvent& ev, ActionList& out);

  LockerState state() const { return state_; }
  AlarmReason alarmReason() const { return alarm_; }
  bool alarmSilenced() const { return false; }  // Silenciar encerra o alarme.
  bool locked() const { return locked_; }
  bool occupied(uint8_t slot) const { return occupied_[slot]; }
  uint16_t sessionUser() const { return user_; }

 private:
  void onTick(ActionList& out);
  void onSlot(uint8_t slot, bool occupied, ActionList& out);
  void onCardGranted(const FsmEvent& ev, ActionList& out);
  void onCardDenied(ActionList& out);
  void onSilence(ActionList& out);
  void onAdminUnlock(const FsmEvent& ev, ActionList& out);
  void onAdminLock(ActionList& out);

  void openFor(uint16_t user, ActionList& out);
  void startSession(uint16_t user, ActionList& out);
  void finishSession(ActionList& out);
  void commitSession(ActionList& out);
  void enterAlarm(AlarmReason reason, ActionList& out);
  void leaveAlarmToIdle(ActionList& out);
  void engage(ActionList& out);
  void note(FsmNote n, ActionList& out);

  FsmConfig cfg_;
  LockerState state_ = LockerState::Idle;
  AlarmReason alarm_ = AlarmReason::None;
  bool locked_ = true;
  bool occupied_[SLOT_COUNT] = {};
  bool initial_[SLOT_COUNT] = {};  // Fotografia dos slots no início da sessão.
  uint16_t user_ = USER_NONE;
  uint32_t now_ = 0;
  uint32_t deadline_ = 0;
};
