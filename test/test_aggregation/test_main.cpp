#include <string.h>
#include <unity.h>

#include "core/usage_stats.h"
#include "storage/aggregator.h"

namespace {
EventRecord event(EventType type, uint16_t user, const char* name, uint32_t duration = 0) {
  EventRecord e{};
  e.type = static_cast<uint8_t>(type);
  e.userId = user;
  e.durationSec = duration;
  strncpy(e.userName, name, sizeof(e.userName) - 1);
  return e;
}
}  // namespace

void setUp() {}
void tearDown() {}

void test_minute_averages_and_keeps_worst_heap() {
  MinuteAggregator m;
  m.add({true, true, false, 10, 20, 100, -50});
  m.add({false, true, false, 30, 40, 80, -60});
  m.add({false, true, true, 50, 60, 120, -70});
  m.add({false, true, false, 10, 20, 90, -60});
  const MinuteResult r = m.finish();
  TEST_ASSERT_EQUAL_UINT8(25, r.unlockedPct);
  TEST_ASSERT_EQUAL_UINT8(100, r.slot1Pct);
  TEST_ASSERT_EQUAL_UINT8(25, r.slot2Pct);
  TEST_ASSERT_EQUAL_UINT8(25, r.cpu0Pct);
  TEST_ASSERT_EQUAL_UINT8(35, r.cpu1Pct);
  TEST_ASSERT_EQUAL_UINT16(80, r.heapMinKb);
  TEST_ASSERT_EQUAL_INT8(-60, r.rssi);
}

void test_minute_resets_after_finish() {
  MinuteAggregator m;
  m.add({true, true, true, 90, 90, 50, -40});
  m.finish();
  TEST_ASSERT_TRUE(m.empty());
  m.add({false, false, false, 10, 10, 200, -80});
  const MinuteResult r = m.finish();
  TEST_ASSERT_EQUAL_UINT8(0, r.unlockedPct);
  TEST_ASSERT_EQUAL_UINT16(200, r.heapMinKb);
}

void test_empty_minute_is_safe() {
  MinuteAggregator m;
  TEST_ASSERT_EQUAL_UINT8(0, m.finish().unlockedPct);
}

void test_hour_average_and_reset() {
  HourAggregator h;
  h.addMinute(100);
  h.addMinute(0);
  TEST_ASSERT_EQUAL_UINT16(2, h.minutes());
  TEST_ASSERT_EQUAL_UINT8(50, h.finish());
  TEST_ASSERT_EQUAL_UINT16(0, h.minutes());
}

void test_hour_and_weekday_histograms() {
  UsageStats u;
  u.add(event(EventType::Withdraw, 1, "Ana"), 9, 1);
  u.add(event(EventType::Withdraw, 1, "Ana"), 9, 2);
  u.add(event(EventType::Withdraw, 2, "Bia"), 14, 1);
  TEST_ASSERT_EQUAL_UINT32(3, u.withdrawals);
  TEST_ASSERT_EQUAL_UINT32(2, u.perHour[9]);
  TEST_ASSERT_EQUAL_UINT32(1, u.perHour[14]);
  TEST_ASSERT_EQUAL_UINT32(2, u.perWeekday[1]);
}

void test_event_without_time_stays_out_of_histogram() {
  UsageStats u;
  u.add(event(EventType::Withdraw, 1, "Ana"), -1, -1);
  TEST_ASSERT_EQUAL_UINT32(1, u.withdrawals);
  uint32_t total = 0;
  for (uint32_t v : u.perHour) total += v;
  TEST_ASSERT_EQUAL_UINT32(0, total);
}

void test_ranking_stays_sorted() {
  UsageStats u;
  u.add(event(EventType::Withdraw, 1, "Ana"), 0, 0);
  u.add(event(EventType::Withdraw, 2, "Bia"), 0, 0);
  u.add(event(EventType::Withdraw, 2, "Bia"), 0, 0);
  u.add(event(EventType::Withdraw, 3, "Caio"), 0, 0);
  u.add(event(EventType::Withdraw, 3, "Caio"), 0, 0);
  u.add(event(EventType::Withdraw, 3, "Caio"), 0, 0);
  TEST_ASSERT_EQUAL_UINT8(3, u.rankedUsers);
  TEST_ASSERT_EQUAL_STRING("Caio", u.ranking[0].name);
  TEST_ASSERT_EQUAL_UINT16(3, u.ranking[0].count);
  TEST_ASSERT_EQUAL_STRING("Bia", u.ranking[1].name);
  TEST_ASSERT_EQUAL_STRING("Ana", u.ranking[2].name);
}

void test_ranking_ignores_unknown_user_and_overflow() {
  UsageStats u;
  u.add(event(EventType::Withdraw, USER_NONE, ""), 0, 0);
  TEST_ASSERT_EQUAL_UINT8(0, u.rankedUsers);
  for (uint16_t id = 1; id <= USAGE_MAX_RANKED + 5; id++) u.add(event(EventType::Withdraw, id, "X"), 0, 0);
  TEST_ASSERT_EQUAL_UINT8(USAGE_MAX_RANKED, u.rankedUsers);
}

void test_average_loan_skips_returns_without_duration() {
  UsageStats u;
  TEST_ASSERT_EQUAL_UINT32(0, u.averageLoanSec());
  u.add(event(EventType::Return, 1, "Ana", 100), 0, 0);
  u.add(event(EventType::Return, 1, "Ana", 300), 0, 0);
  u.add(event(EventType::Return, 1, "Ana", 0), 0, 0);
  TEST_ASSERT_EQUAL_UINT32(3, u.returns);
  TEST_ASSERT_EQUAL_UINT32(200, u.averageLoanSec());
}

void test_violations_and_denials_are_counted() {
  UsageStats u;
  u.add(event(EventType::Denied, USER_NONE, ""), 0, 0);
  u.add(event(EventType::Violation, USER_NONE, ""), 0, 0);
  u.add(event(EventType::Intrusion, USER_NONE, ""), 0, 0);
  TEST_ASSERT_EQUAL_UINT32(1, u.denied);
  TEST_ASSERT_EQUAL_UINT32(2, u.violations);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_minute_averages_and_keeps_worst_heap);
  RUN_TEST(test_minute_resets_after_finish);
  RUN_TEST(test_empty_minute_is_safe);
  RUN_TEST(test_hour_average_and_reset);
  RUN_TEST(test_hour_and_weekday_histograms);
  RUN_TEST(test_event_without_time_stays_out_of_histogram);
  RUN_TEST(test_ranking_stays_sorted);
  RUN_TEST(test_ranking_ignores_unknown_user_and_overflow);
  RUN_TEST(test_average_loan_skips_returns_without_duration);
  RUN_TEST(test_violations_and_denials_are_counted);
  return UNITY_END();
}
