#include "settings_model.h"

#include <stdio.h>
#include <string.h>

namespace {

template <size_t N>
void copyStr(char (&dst)[N], const char* src) {
  strncpy(dst, src, N - 1);
  dst[N - 1] = '\0';
}

template <size_t N>
void terminate(char (&s)[N]) {
  s[N - 1] = '\0';
}

// Aceita string vazia (IP estático desligado) ou IPv4 com octetos válidos.
bool validIpv4(const char* s) {
  if (s[0] == '\0') return true;
  unsigned a, b, c, d;
  char extra;
  if (sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4) return false;
  return a <= 255 && b <= 255 && c <= 255 && d <= 255;
}

struct Parser {
  JsonObjectConst in;
  char* err;
  size_t errLen;
  bool ok = true;

  void fail(const char* key) {
    if (ok) snprintf(err, errLen, "campo invalido: %s", key);
    ok = false;
  }

  // Lê para long em uma única função: a versão template gerava uma cópia do
  // código do ArduinoJson para cada tipo e dobrava o tamanho deste arquivo.
  bool readNumber(const char* key, long min, long max, long* value) {
    JsonVariantConst v = in[key];
    if (v.isNull()) return false;
    if (!v.is<long>()) {
      fail(key);
      return false;
    }
    *value = v.as<long>();
    if (*value < min || *value > max) {
      fail(key);
      return false;
    }
    return true;
  }

  template <typename T>
  void number(const char* key, T& out, long min, long max) {
    long value;
    if (readNumber(key, min, max, &value)) out = static_cast<T>(value);
  }

  void boolean(const char* key, bool& out) {
    JsonVariantConst v = in[key];
    if (v.isNull()) return;
    if (!v.is<bool>()) return fail(key);
    out = v.as<bool>();
  }

  void textInto(const char* key, char* out, size_t capacity) {
    JsonVariantConst v = in[key];
    if (v.isNull()) return;
    if (!v.is<const char*>()) return fail(key);
    const char* value = v.as<const char*>();
    if (strlen(value) >= capacity) return fail(key);
    strcpy(out, value);
  }

  template <size_t N>
  void text(const char* key, char (&out)[N]) {
    textInto(key, out, N);
  }

  void ip(const char* key, char* out, size_t capacity) {
    JsonVariantConst v = in[key];
    if (!v.isNull() && (!v.is<const char*>() || !validIpv4(v.as<const char*>()))) return fail(key);
    textInto(key, out, capacity);
  }
};

}  // namespace

void settingsDefaults(Settings& s) {
  memset(&s, 0, sizeof(s));
  s.magic = SETTINGS_MAGIC;
  s.schema = SETTINGS_SCHEMA;
  s.sensorSampleMs = 5000;
  s.nfcPollMs = 200;
  s.unlockSec = 15;
  s.relockDelaySec = 3;
  s.servoLockedDeg = 0;
  s.servoUnlockedDeg = 90;
  s.beepVolume = 100;
  s.beepPattern = 1;
  s.hoursEnforced = false;
  s.openHour = 7;
  s.closeHour = 22;
  s.maxLoanHours = 24;
  s.eventRetentionDays = 30;
  copyStr(s.timezone, "<-03>3");  // America/Sao_Paulo em POSIX TZ, sem horário de verão.
  copyStr(s.ntpServer, "pool.ntp.org");
  s.cpuMhz = 240;
  s.modemSleep = false;
  s.autoEco = false;
  s.publicPages = true;
}

bool settingsSanitize(Settings& s) {
  if (s.magic != SETTINGS_MAGIC || s.schema != SETTINGS_SCHEMA) {
    settingsDefaults(s);
    return true;
  }

  terminate(s.wifiSsid);
  terminate(s.wifiPass);
  terminate(s.ip);
  terminate(s.gateway);
  terminate(s.subnet);
  terminate(s.dns);
  terminate(s.timezone);
  terminate(s.ntpServer);
  for (auto& asset : s.slotAsset) terminate(asset);

  Settings d;
  settingsDefaults(d);
  auto within = [](auto& v, long min, long max, auto fallback) {
    if (long(v) < min || long(v) > max) v = fallback;
  };
  within(s.sensorSampleMs, 1000, 60000, d.sensorSampleMs);
  within(s.nfcPollMs, 50, 2000, d.nfcPollMs);
  within(s.unlockSec, 3, 120, d.unlockSec);
  within(s.relockDelaySec, 1, 60, d.relockDelaySec);
  within(s.servoLockedDeg, 0, 180, d.servoLockedDeg);
  within(s.servoUnlockedDeg, 0, 180, d.servoUnlockedDeg);
  within(s.beepVolume, 0, 100, d.beepVolume);
  within(s.beepPattern, 0, 2, d.beepPattern);
  within(s.openHour, 0, 23, d.openHour);
  within(s.closeHour, 1, 24, d.closeHour);
  within(s.maxLoanHours, 1, 720, d.maxLoanHours);
  within(s.eventRetentionDays, 1, 365, d.eventRetentionDays);
  if (s.cpuMhz != 80 && s.cpuMhz != 160 && s.cpuMhz != 240) s.cpuMhz = d.cpuMhz;
  if (!validIpv4(s.ip) || !validIpv4(s.gateway) || !validIpv4(s.subnet) || !validIpv4(s.dns)) s.staticIp = false;
  if (s.timezone[0] == '\0') memcpy(s.timezone, d.timezone, sizeof(s.timezone));
  if (s.ntpServer[0] == '\0') memcpy(s.ntpServer, d.ntpServer, sizeof(s.ntpServer));
  return false;
}

bool settingsApplyJson(JsonObjectConst in, Settings& s, char* err, size_t errLen) {
  Settings next = s;
  Parser p{in, err, errLen};

  p.number("sensorSampleMs", next.sensorSampleMs, 1000, 60000);
  p.number("nfcPollMs", next.nfcPollMs, 50, 2000);
  p.number("unlockSec", next.unlockSec, 3, 120);
  p.number("relockDelaySec", next.relockDelaySec, 1, 60);
  p.number("servoLockedDeg", next.servoLockedDeg, 0, 180);
  p.number("servoUnlockedDeg", next.servoUnlockedDeg, 0, 180);
  p.number("beepVolume", next.beepVolume, 0, 100);
  p.number("beepPattern", next.beepPattern, 0, 2);
  p.boolean("hoursEnforced", next.hoursEnforced);
  p.number("openHour", next.openHour, 0, 23);
  p.number("closeHour", next.closeHour, 1, 24);
  p.number("maxLoanHours", next.maxLoanHours, 1, 720);
  p.number("eventRetentionDays", next.eventRetentionDays, 1, 365);
  p.text("wifiSsid", next.wifiSsid);
  p.text("wifiPass", next.wifiPass);
  p.boolean("staticIp", next.staticIp);
  p.ip("ip", next.ip, sizeof(next.ip));
  p.ip("gateway", next.gateway, sizeof(next.gateway));
  p.ip("subnet", next.subnet, sizeof(next.subnet));
  p.ip("dns", next.dns, sizeof(next.dns));
  p.text("timezone", next.timezone);
  p.text("ntpServer", next.ntpServer);
  p.number("cpuMhz", next.cpuMhz, 80, 240);
  p.boolean("modemSleep", next.modemSleep);
  p.boolean("autoEco", next.autoEco);
  p.boolean("publicPages", next.publicPages);

  JsonArrayConst slots = in["slots"];
  if (!slots.isNull()) {
    size_t i = 0;
    for (JsonObjectConst slot : slots) {
      if (i >= SLOT_COUNT) break;
      Parser sp{slot, err, errLen};
      sp.text("asset", next.slotAsset[i]);
      sp.boolean("maintenance", next.slotMaintenance[i]);
      if (!sp.ok) p.ok = false;
      i++;
    }
  }

  if (p.ok && next.cpuMhz != 80 && next.cpuMhz != 160 && next.cpuMhz != 240) p.fail("cpuMhz");
  if (p.ok && next.openHour >= next.closeHour) p.fail("closeHour");
  if (p.ok && next.servoLockedDeg == next.servoUnlockedDeg) p.fail("servoUnlockedDeg");
  if (p.ok && next.staticIp && (next.ip[0] == '\0' || next.gateway[0] == '\0' || next.subnet[0] == '\0')) p.fail("ip");
  if (p.ok && next.wifiPass[0] != '\0' && strlen(next.wifiPass) < 8) p.fail("wifiPass");
  if (!p.ok) return false;

  s = next;
  return true;
}

void settingsToJson(const Settings& s, JsonObject out, bool includeSecrets) {
  out["sensorSampleMs"] = s.sensorSampleMs;
  out["nfcPollMs"] = s.nfcPollMs;
  out["unlockSec"] = s.unlockSec;
  out["relockDelaySec"] = s.relockDelaySec;
  out["servoLockedDeg"] = s.servoLockedDeg;
  out["servoUnlockedDeg"] = s.servoUnlockedDeg;
  out["beepVolume"] = s.beepVolume;
  out["beepPattern"] = s.beepPattern;
  out["hoursEnforced"] = s.hoursEnforced;
  out["openHour"] = s.openHour;
  out["closeHour"] = s.closeHour;
  out["maxLoanHours"] = s.maxLoanHours;
  out["eventRetentionDays"] = s.eventRetentionDays;
  out["wifiSsid"] = static_cast<const char*>(s.wifiSsid);
  if (includeSecrets) out["wifiPass"] = static_cast<const char*>(s.wifiPass);
  out["staticIp"] = s.staticIp;
  out["ip"] = static_cast<const char*>(s.ip);
  out["gateway"] = static_cast<const char*>(s.gateway);
  out["subnet"] = static_cast<const char*>(s.subnet);
  out["dns"] = static_cast<const char*>(s.dns);
  out["timezone"] = static_cast<const char*>(s.timezone);
  out["ntpServer"] = static_cast<const char*>(s.ntpServer);
  out["cpuMhz"] = s.cpuMhz;
  out["modemSleep"] = s.modemSleep;
  out["autoEco"] = s.autoEco;
  out["publicPages"] = s.publicPages;
  JsonArray slots = out["slots"].to<JsonArray>();
  for (size_t i = 0; i < SLOT_COUNT; i++) {
    JsonObject slot = slots.add<JsonObject>();
    slot["asset"] = static_cast<const char*>(s.slotAsset[i]);
    slot["maintenance"] = s.slotMaintenance[i];
  }
}
