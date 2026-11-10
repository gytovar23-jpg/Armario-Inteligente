#include <string.h>
#include <unity.h>

#include "storage/settings_model.h"

namespace {
Settings s;
char err[64];

bool apply(const char* json) {
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, json));
  err[0] = '\0';
  return settingsApplyJson(doc.as<JsonObjectConst>(), s, err, sizeof(err));
}
}  // namespace

void setUp() { settingsDefaults(s); }
void tearDown() {}

void test_defaults_are_valid() {
  TEST_ASSERT_FALSE(settingsSanitize(s));
  TEST_ASSERT_EQUAL_UINT16(240, s.cpuMhz);
  TEST_ASSERT_EQUAL_STRING("<-03>3", s.timezone);
}

void test_partial_json_changes_only_given_fields() {
  TEST_ASSERT_TRUE(apply("{\"unlockSec\":20}"));
  TEST_ASSERT_EQUAL_UINT16(20, s.unlockSec);
  TEST_ASSERT_EQUAL_UINT16(3, s.relockDelaySec);
}

void test_out_of_range_is_rejected_and_names_the_field() {
  TEST_ASSERT_FALSE(apply("{\"nfcPollMs\":1}"));
  TEST_ASSERT_NOT_NULL(strstr(err, "nfcPollMs"));
  TEST_ASSERT_EQUAL_UINT16(200, s.nfcPollMs);
}

void test_rejection_is_all_or_nothing() {
  TEST_ASSERT_FALSE(apply("{\"unlockSec\":20,\"relockDelaySec\":0}"));
  TEST_ASSERT_EQUAL_UINT16(15, s.unlockSec);
}

void test_wrong_type_is_rejected() {
  TEST_ASSERT_FALSE(apply("{\"unlockSec\":\"vinte\"}"));
  TEST_ASSERT_FALSE(apply("{\"modemSleep\":1}"));
  TEST_ASSERT_FALSE(apply("{\"timezone\":5}"));
}

void test_only_supported_cpu_frequencies() {
  TEST_ASSERT_TRUE(apply("{\"cpuMhz\":160}"));
  TEST_ASSERT_FALSE(apply("{\"cpuMhz\":100}"));
  TEST_ASSERT_EQUAL_UINT16(160, s.cpuMhz);
}

void test_hours_must_be_ordered() {
  TEST_ASSERT_FALSE(apply("{\"openHour\":22,\"closeHour\":7}"));
  TEST_ASSERT_TRUE(apply("{\"openHour\":0,\"closeHour\":24}"));
}

void test_servo_angles_must_differ() { TEST_ASSERT_FALSE(apply("{\"servoLockedDeg\":90,\"servoUnlockedDeg\":90}")); }

void test_static_ip_requires_addresses() {
  TEST_ASSERT_FALSE(apply("{\"staticIp\":true}"));
  TEST_ASSERT_FALSE(apply("{\"staticIp\":true,\"ip\":\"192.168.1.300\",\"gateway\":\"192.168.1.1\",\"subnet\":\"255.255.255.0\"}"));
  TEST_ASSERT_TRUE(apply("{\"staticIp\":true,\"ip\":\"192.168.1.50\",\"gateway\":\"192.168.1.1\",\"subnet\":\"255.255.255.0\"}"));
}

void test_wifi_text_limits() {
  TEST_ASSERT_TRUE(apply("{\"wifiSsid\":\"LabRede\",\"wifiPass\":\"12345678\"}"));
  TEST_ASSERT_EQUAL_STRING("LabRede", s.wifiSsid);
  TEST_ASSERT_FALSE(apply("{\"wifiPass\":\"curta\"}"));
  TEST_ASSERT_FALSE(apply("{\"wifiSsid\":\"123456789012345678901234567890123\"}"));
}

void test_slots_are_parsed() {
  TEST_ASSERT_TRUE(apply("{\"slots\":[{\"asset\":\"NB-01\",\"maintenance\":true},{\"asset\":\"NB-02\"}]}"));
  TEST_ASSERT_EQUAL_STRING("NB-01", s.slotAsset[0]);
  TEST_ASSERT_TRUE(s.slotMaintenance[0]);
  TEST_ASSERT_FALSE(s.slotMaintenance[1]);
}

void test_invalid_slot_rejects_everything() {
  TEST_ASSERT_FALSE(apply("{\"unlockSec\":20,\"slots\":[{\"asset\":\"123456789012345678901234567890\"}]}"));
  TEST_ASSERT_EQUAL_UINT16(15, s.unlockSec);
}

void test_sanitize_resets_unknown_blob() {
  s.magic = 0;
  s.unlockSec = 9999;
  TEST_ASSERT_TRUE(settingsSanitize(s));
  TEST_ASSERT_EQUAL_UINT16(15, s.unlockSec);
  TEST_ASSERT_EQUAL_UINT32(SETTINGS_MAGIC, s.magic);
}

void test_sanitize_repairs_single_field() {
  s.unlockSec = 9999;
  s.cpuMhz = 123;
  memset(s.wifiSsid, 'x', sizeof(s.wifiSsid));
  TEST_ASSERT_FALSE(settingsSanitize(s));
  TEST_ASSERT_EQUAL_UINT16(15, s.unlockSec);
  TEST_ASSERT_EQUAL_UINT16(240, s.cpuMhz);
  TEST_ASSERT_EQUAL_size_t(sizeof(s.wifiSsid) - 1, strlen(s.wifiSsid));
}

void test_secrets_stay_out_of_json_export() {
  strcpy(s.wifiPass, "segredo123");
  JsonDocument doc;
  settingsToJson(s, doc.to<JsonObject>());
  TEST_ASSERT_TRUE(doc["wifiPass"].isNull());
}

void test_json_roundtrip() {
  TEST_ASSERT_TRUE(apply("{\"unlockSec\":33,\"cpuMhz\":80,\"modemSleep\":true,\"slots\":[{\"asset\":\"A\"},{\"asset\":\"B\"}]}"));
  JsonDocument doc;
  settingsToJson(s, doc.to<JsonObject>());
  Settings copy;
  settingsDefaults(copy);
  TEST_ASSERT_TRUE(settingsApplyJson(doc.as<JsonObjectConst>(), copy, err, sizeof(err)));
  TEST_ASSERT_EQUAL_UINT16(33, copy.unlockSec);
  TEST_ASSERT_EQUAL_UINT16(80, copy.cpuMhz);
  TEST_ASSERT_TRUE(copy.modemSleep);
  TEST_ASSERT_EQUAL_STRING("B", copy.slotAsset[1]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_are_valid);
  RUN_TEST(test_partial_json_changes_only_given_fields);
  RUN_TEST(test_out_of_range_is_rejected_and_names_the_field);
  RUN_TEST(test_rejection_is_all_or_nothing);
  RUN_TEST(test_wrong_type_is_rejected);
  RUN_TEST(test_only_supported_cpu_frequencies);
  RUN_TEST(test_hours_must_be_ordered);
  RUN_TEST(test_servo_angles_must_differ);
  RUN_TEST(test_static_ip_requires_addresses);
  RUN_TEST(test_wifi_text_limits);
  RUN_TEST(test_slots_are_parsed);
  RUN_TEST(test_invalid_slot_rejects_everything);
  RUN_TEST(test_sanitize_resets_unknown_blob);
  RUN_TEST(test_sanitize_repairs_single_field);
  RUN_TEST(test_secrets_stay_out_of_json_export);
  RUN_TEST(test_json_roundtrip);
  return UNITY_END();
}
