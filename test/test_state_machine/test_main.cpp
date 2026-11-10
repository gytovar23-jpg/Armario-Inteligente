// Cenários do armário sem sensor de porta, sem hardware: entra evento, sai ação.
#include <unity.h>

#include "core/state_machine.h"

namespace {
constexpr uint16_t ALICE = 1;
constexpr uint16_t BOB = 2;
constexpr uint32_t UNLOCK_MS = 15000;
constexpr uint32_t RELOCK_MS = 3000;

StateMachine fsm;
ActionList out;
uint32_t now;

void start(bool slot1 = true, bool slot2 = true) {
  fsm = StateMachine();
  FsmConfig cfg;
  cfg.unlockMs = UNLOCK_MS;
  cfg.relockMs = RELOCK_MS;
  fsm.setConfig(cfg);
  out.clear();
  now = 1000;
  const bool occupied[SLOT_COUNT] = {slot1, slot2};
  fsm.begin(now, occupied, out);
}

void send(FsmInput type, uint8_t slot = 0, uint16_t user = USER_NONE, bool admin = false) {
  out.clear();
  FsmEvent ev{type, now};
  ev.slot = slot;
  ev.userId = user;
  ev.admin = admin;
  fsm.handle(ev, out);
}

void wait(uint32_t ms) {
  now += ms;
  send(FsmInput::Tick);
}

bool has(ActionType type) {
  for (uint8_t i = 0; i < out.count; i++) {
    if (out.items[i].type == type) return true;
  }
  return false;
}

const FsmAction* find(ActionType type) {
  for (uint8_t i = 0; i < out.count; i++) {
    if (out.items[i].type == type) return &out.items[i];
  }
  return nullptr;
}

bool hasNote(FsmNote note) {
  for (uint8_t i = 0; i < out.count; i++) {
    if (out.items[i].type == ActionType::Note && out.items[i].note == note) return true;
  }
  return false;
}

void assertState(LockerState expected) { TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(fsm.state())); }
}  // namespace

void setUp() { start(); }
void tearDown() {}

void test_boots_idle_and_locked() {
  assertState(LockerState::Idle);
  TEST_ASSERT_TRUE(fsm.locked());
  TEST_ASSERT_TRUE(has(ActionType::EngageLock));
}

void test_denied_card_keeps_lock_closed() {
  send(FsmInput::CardDenied);
  assertState(LockerState::Idle);
  TEST_ASSERT_TRUE(has(ActionType::FeedbackDenied));
  TEST_ASSERT_FALSE(has(ActionType::ReleaseLock));
}

void test_granted_card_opens_session() {
  send(FsmInput::CardGranted, 0, ALICE);
  assertState(LockerState::Open);
  TEST_ASSERT_TRUE(has(ActionType::ReleaseLock));
  TEST_ASSERT_TRUE(has(ActionType::FeedbackGranted));
  TEST_ASSERT_TRUE(has(ActionType::LightOn));
  TEST_ASSERT_TRUE(hasNote(FsmNote::SessionStarted));
  TEST_ASSERT_FALSE(fsm.locked());
  TEST_ASSERT_EQUAL_UINT16(ALICE, fsm.sessionUser());
}

void test_session_without_changes_relocks_after_window() {
  send(FsmInput::CardGranted, 0, ALICE);
  wait(UNLOCK_MS - 1);
  assertState(LockerState::Open);
  wait(1);
  assertState(LockerState::Idle);
  TEST_ASSERT_TRUE(has(ActionType::EngageLock));
  TEST_ASSERT_TRUE(has(ActionType::LightOff));
  TEST_ASSERT_TRUE(hasNote(FsmNote::SessionEnded));
  TEST_ASSERT_FALSE(has(ActionType::CommitWithdraw));
  TEST_ASSERT_FALSE(has(ActionType::CommitReturn));
}

void test_withdraw_is_committed_only_after_relock() {
  send(FsmInput::CardGranted, 0, ALICE);
  send(FsmInput::SlotFreed, 0);
  TEST_ASSERT_FALSE(has(ActionType::CommitWithdraw));
  TEST_ASSERT_FALSE(has(ActionType::SlotViolation));
  wait(RELOCK_MS);
  assertState(LockerState::Idle);
  const FsmAction* commit = find(ActionType::CommitWithdraw);
  TEST_ASSERT_NOT_NULL(commit);
  TEST_ASSERT_EQUAL_UINT8(0, commit->slot);
  TEST_ASSERT_EQUAL_UINT16(ALICE, commit->userId);
  TEST_ASSERT_TRUE(has(ActionType::EngageLock));
}

void test_slot_change_shortens_window_to_relock_delay() {
  send(FsmInput::CardGranted, 0, ALICE);
  wait(1000);
  send(FsmInput::SlotFreed, 0);
  wait(RELOCK_MS - 1);
  assertState(LockerState::Open);
  wait(1);
  assertState(LockerState::Idle);
}

void test_slot_change_near_end_extends_window() {
  send(FsmInput::CardGranted, 0, ALICE);
  wait(UNLOCK_MS - 500);
  send(FsmInput::SlotFreed, 0);
  wait(1000);
  assertState(LockerState::Open);  // O prazo original já passou; a mudança o estendeu.
  wait(RELOCK_MS);
  assertState(LockerState::Idle);
  TEST_ASSERT_NOT_NULL(find(ActionType::CommitWithdraw));
}

void test_return_is_committed() {
  start(false, true);
  send(FsmInput::CardGranted, 0, ALICE);
  send(FsmInput::SlotOccupied, 0);
  wait(RELOCK_MS);
  TEST_ASSERT_NOT_NULL(find(ActionType::CommitReturn));
}

void test_taking_and_replacing_is_not_a_loan() {
  send(FsmInput::CardGranted, 0, ALICE);
  send(FsmInput::SlotFreed, 0);
  send(FsmInput::SlotOccupied, 0);
  wait(RELOCK_MS);
  TEST_ASSERT_FALSE(has(ActionType::CommitWithdraw));
  TEST_ASSERT_FALSE(has(ActionType::CommitReturn));
}

void test_swap_between_slots_records_both() {
  start(true, false);
  send(FsmInput::CardGranted, 0, ALICE);
  send(FsmInput::SlotFreed, 0);
  send(FsmInput::SlotOccupied, 1);
  wait(RELOCK_MS);
  TEST_ASSERT_NOT_NULL(find(ActionType::CommitWithdraw));
  TEST_ASSERT_NOT_NULL(find(ActionType::CommitReturn));
}

void test_same_card_again_restarts_window() {
  send(FsmInput::CardGranted, 0, ALICE);
  wait(10000);
  send(FsmInput::CardGranted, 0, ALICE);
  TEST_ASSERT_TRUE(has(ActionType::FeedbackGranted));
  TEST_ASSERT_FALSE(hasNote(FsmNote::SessionStarted));
  wait(10000);
  assertState(LockerState::Open);
  wait(UNLOCK_MS - 10000);
  assertState(LockerState::Idle);
}

void test_next_user_card_keeps_credit_with_first_user() {
  send(FsmInput::CardGranted, 0, ALICE);
  send(FsmInput::SlotFreed, 0);
  send(FsmInput::CardGranted, 0, BOB);
  const FsmAction* commit = find(ActionType::CommitWithdraw);
  TEST_ASSERT_NOT_NULL(commit);
  TEST_ASSERT_EQUAL_UINT16(ALICE, commit->userId);
  TEST_ASSERT_TRUE(hasNote(FsmNote::SessionStarted));
  assertState(LockerState::Open);
  TEST_ASSERT_EQUAL_UINT16(BOB, fsm.sessionUser());

  send(FsmInput::SlotFreed, 1);
  wait(RELOCK_MS);
  commit = find(ActionType::CommitWithdraw);
  TEST_ASSERT_NOT_NULL(commit);
  TEST_ASSERT_EQUAL_UINT8(1, commit->slot);
  TEST_ASSERT_EQUAL_UINT16(BOB, commit->userId);
}

void test_slot_changed_while_locked_is_violation() {
  send(FsmInput::SlotFreed, 1);
  assertState(LockerState::Alarm);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AlarmReason::Violation), static_cast<int>(fsm.alarmReason()));
  TEST_ASSERT_TRUE(has(ActionType::AlarmOn));
  const FsmAction* v = find(ActionType::SlotViolation);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_EQUAL_UINT8(1, v->slot);
}

void test_more_changes_during_alarm_are_recorded_once_each() {
  send(FsmInput::SlotFreed, 1);
  send(FsmInput::SlotFreed, 0);
  TEST_ASSERT_NOT_NULL(find(ActionType::SlotViolation));
  TEST_ASSERT_FALSE(has(ActionType::AlarmOn));
  assertState(LockerState::Alarm);
}

void test_silence_ends_alarm_and_stays_locked() {
  send(FsmInput::SlotFreed, 1);
  send(FsmInput::SilenceAlarm);
  TEST_ASSERT_TRUE(has(ActionType::AlarmOff));
  assertState(LockerState::Idle);
  TEST_ASSERT_TRUE(fsm.locked());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AlarmReason::None), static_cast<int>(fsm.alarmReason()));
}

void test_common_card_does_not_silence_alarm() {
  send(FsmInput::SlotFreed, 1);
  send(FsmInput::CardGranted, 0, ALICE, false);
  assertState(LockerState::Alarm);
  TEST_ASSERT_TRUE(has(ActionType::FeedbackDenied));
  TEST_ASSERT_FALSE(has(ActionType::ReleaseLock));
}

void test_admin_card_silences_alarm() {
  send(FsmInput::SlotFreed, 1);
  send(FsmInput::CardGranted, 0, BOB, true);
  TEST_ASSERT_TRUE(has(ActionType::AlarmOff));
  assertState(LockerState::Idle);
}

void test_enroll_captures_and_times_out() {
  send(FsmInput::EnrollStart);
  assertState(LockerState::Enroll);
  send(FsmInput::EnrollCaptured);
  assertState(LockerState::Idle);
  TEST_ASSERT_TRUE(has(ActionType::FeedbackEnrollDone));

  send(FsmInput::EnrollStart);
  wait(ENROLL_TIMEOUT_MS);
  assertState(LockerState::Idle);
  TEST_ASSERT_TRUE(hasNote(FsmNote::EnrollTimedOut));
}

void test_enroll_refused_outside_idle() {
  send(FsmInput::CardGranted, 0, ALICE);
  send(FsmInput::EnrollStart);
  assertState(LockerState::Open);
  TEST_ASSERT_TRUE(hasNote(FsmNote::Refused));
}

void test_maintenance_slot_ignores_changes() {
  FsmConfig cfg;
  cfg.maintenance[0] = true;
  fsm.setConfig(cfg);
  send(FsmInput::SlotFreed, 0);
  assertState(LockerState::Idle);
  TEST_ASSERT_TRUE(hasNote(FsmNote::SlotInMaintenance));
}

void test_admin_unlock_and_lock() {
  send(FsmInput::AdminUnlock, 0, USER_WEB_ADMIN);
  assertState(LockerState::Open);
  TEST_ASSERT_TRUE(has(ActionType::ReleaseLock));
  send(FsmInput::SlotFreed, 0);
  send(FsmInput::AdminLock);
  assertState(LockerState::Idle);
  TEST_ASSERT_TRUE(fsm.locked());
  const FsmAction* commit = find(ActionType::CommitWithdraw);
  TEST_ASSERT_NOT_NULL(commit);
  TEST_ASSERT_EQUAL_UINT16(USER_WEB_ADMIN, commit->userId);
}

void test_admin_lock_refused_when_idle() {
  send(FsmInput::AdminLock);
  assertState(LockerState::Idle);
  TEST_ASSERT_TRUE(hasNote(FsmNote::Refused));
}

void test_admin_unlock_refused_during_alarm() {
  send(FsmInput::SlotFreed, 1);
  send(FsmInput::AdminUnlock, 0, USER_WEB_ADMIN);
  assertState(LockerState::Alarm);
  TEST_ASSERT_TRUE(hasNote(FsmNote::Refused));
}

void test_timer_survives_millis_rollover() {
  start();
  now = 0xFFFFF000;
  send(FsmInput::CardGranted, 0, ALICE);
  wait(1000);
  assertState(LockerState::Open);
  wait(UNLOCK_MS - 1000);
  assertState(LockerState::Idle);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_boots_idle_and_locked);
  RUN_TEST(test_denied_card_keeps_lock_closed);
  RUN_TEST(test_granted_card_opens_session);
  RUN_TEST(test_session_without_changes_relocks_after_window);
  RUN_TEST(test_withdraw_is_committed_only_after_relock);
  RUN_TEST(test_slot_change_shortens_window_to_relock_delay);
  RUN_TEST(test_slot_change_near_end_extends_window);
  RUN_TEST(test_return_is_committed);
  RUN_TEST(test_taking_and_replacing_is_not_a_loan);
  RUN_TEST(test_swap_between_slots_records_both);
  RUN_TEST(test_same_card_again_restarts_window);
  RUN_TEST(test_next_user_card_keeps_credit_with_first_user);
  RUN_TEST(test_slot_changed_while_locked_is_violation);
  RUN_TEST(test_more_changes_during_alarm_are_recorded_once_each);
  RUN_TEST(test_silence_ends_alarm_and_stays_locked);
  RUN_TEST(test_common_card_does_not_silence_alarm);
  RUN_TEST(test_admin_card_silences_alarm);
  RUN_TEST(test_enroll_captures_and_times_out);
  RUN_TEST(test_enroll_refused_outside_idle);
  RUN_TEST(test_maintenance_slot_ignores_changes);
  RUN_TEST(test_admin_unlock_and_lock);
  RUN_TEST(test_admin_lock_refused_when_idle);
  RUN_TEST(test_admin_unlock_refused_during_alarm);
  RUN_TEST(test_timer_survives_millis_rollover);
  return UNITY_END();
}
