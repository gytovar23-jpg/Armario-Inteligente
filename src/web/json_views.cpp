#include "json_views.h"

#include <Arduino.h>
#include <esp_system.h>

#include "core/shared_state.h"
#include "core/timebase.h"
#include "core/usage_store.h"
#include "drivers/nfc.h"
#include "perf/cpu_usage.h"
#include "perf/memory.h"
#include "perf/profiler.h"
#include "power/power.h"
#include "storage/logger.h"
#include "storage/settings.h"
#include "storage/storage.h"
#include "version.h"
#include "wifi_mgr.h"

namespace web {

const char* stateName(uint8_t state) {
  static const char* const NAMES[] = {"IDLE", "OPEN", "ALARM", "ENROLL"};
  return NAMES[state];
}

const char* alarmName(uint8_t alarm) {
  static const char* const NAMES[] = {"", "VIOLACAO"};
  return NAMES[alarm];
}

const char* eventTypeName(uint8_t type) {
  static const char* const NAMES[] = {"retirada", "devolucao", "negado", "violacao", "intrusao"};
  return type < 5 ? NAMES[type] : "?";
}

void stateToJson(JsonObject out) {
  PROFILE_SCOPE(ProfId::JsonSerialize);
  const LockerSnapshot s = shared::read();
  const Settings cfg = settings::get();

  out["state"] = stateName(static_cast<uint8_t>(s.state));
  out["alarm"] = alarmName(static_cast<uint8_t>(s.alarm));
  out["silenced"] = s.alarmSilenced;
  out["locked"] = s.locked;
  // ArduinoJson 7 guarda um const char[N] como literal: só o ponteiro, com N-1 bytes,
  // lixo após o \0 incluso. O cast faz copiar até o \0, e a cópia local pode sair de escopo.
  out["user"] = static_cast<const char*>(s.sessionUser);
  out["timeSynced"] = timebase::synced();
  uint8_t flags = 0;
  out["now"] = timebase::now(&flags);

  JsonArray slots = out["slots"].to<JsonArray>();
  for (uint8_t i = 0; i < SLOT_COUNT; i++) {
    JsonObject slot = slots.add<JsonObject>();
    slot["id"] = i + 1;
    slot["occupied"] = s.slotOccupied[i];
    slot["asset"] = static_cast<const char*>(cfg.slotAsset[i]);
    slot["maintenance"] = cfg.slotMaintenance[i];
    slot["overdue"] = s.overdue[i];
    if (s.loans[i].active) {
      JsonObject loan = slot["loan"].to<JsonObject>();
      loan["user"] = static_cast<const char*>(s.loans[i].userName);
      loan["since"] = s.loans[i].since;
      loan["synced"] = (s.loans[i].flags & REC_TIME_SYNCED) != 0;
    }
  }
}

void usageToJson(JsonObject out) {
  PROFILE_SCOPE(ProfId::JsonSerialize);
  // Estática: a estrutura tem ~600 B, muito para a pilha da task do AsyncTCP, e
  // só essa task chama aqui.
  static UsageStats u;
  usage::copy(&u);

  out["withdrawals"] = u.withdrawals;
  out["returns"] = u.returns;
  out["denied"] = u.denied;
  out["violations"] = u.violations;
  out["avgLoanSec"] = u.averageLoanSec();
  JsonArray hours = out["perHour"].to<JsonArray>();
  for (uint32_t v : u.perHour) hours.add(v);
  JsonArray days = out["perWeekday"].to<JsonArray>();
  for (uint32_t v : u.perWeekday) days.add(v);
  JsonArray ranking = out["ranking"].to<JsonArray>();
  for (uint8_t i = 0; i < u.rankedUsers && i < 10; i++) {
    JsonObject row = ranking.add<JsonObject>();
    row["name"] = u.ranking[i].name;
    row["count"] = u.ranking[i].count;
  }

  const LockerSnapshot s = shared::read();
  const Settings cfg = settings::get();
  uint8_t inUse = 0;
  uint8_t usable = 0;
  for (uint8_t i = 0; i < SLOT_COUNT; i++) {
    if (cfg.slotMaintenance[i]) continue;
    usable++;
    if (!s.slotOccupied[i]) inUse++;
  }
  out["occupancyPct"] = usable ? inUse * 100 / usable : 0;
}

namespace {
const char* resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
      return "energia";
    case ESP_RST_EXT:
      return "pino externo";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panico";
    case ESP_RST_INT_WDT:
      return "watchdog int.";
    case ESP_RST_TASK_WDT:
      return "watchdog task";
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_DEEPSLEEP:
      return "deep sleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    default:
      return "desconhecido";
  }
}

// Escala usual para Wi-Fi: -100 dBm é sem sinal e -50 dBm é sinal pleno.
uint8_t signalPercent(int rssi) { return constrain(2 * (rssi + 100), 0, 100); }

uint8_t signalBars(int rssi) {
  if (rssi >= -55) return 4;
  if (rssi >= -65) return 3;
  if (rssi >= -75) return 2;
  return rssi >= -85 ? 1 : 0;
}
}  // namespace

void perfToJson(JsonObject out) {
  PROFILE_SCOPE(ProfId::JsonSerialize);
  const Settings cfg = settings::get();
  const LockerSnapshot s = shared::read();

  out["uptimeSec"] = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
  out["cpuMhz"] = getCpuFrequencyMhz();
  out["resetReason"] = resetReasonText();
  out["firmware"] = FW_VERSION;

  JsonArray cpu = out["cpu"].to<JsonArray>();
  cpu.add(shared::cpuPct0.load());
  cpu.add(shared::cpuPct1.load());

  const perfmem::Snapshot m = perfmem::read();
  JsonObject mem = out["mem"].to<JsonObject>();
  mem["heapFree"] = m.heapFree;
  mem["heapTotal"] = m.heapTotal;
  mem["heapMin"] = m.heapMin;
  mem["heapLargest"] = m.heapLargest;
  mem["fragPct"] = m.fragmentationPct;
  JsonObject flash = out["flash"].to<JsonObject>();
  flash["size"] = m.flashSize;
  flash["appUsed"] = m.appUsed;
  flash["appMax"] = m.appMax;
  flash["fsTotal"] = m.fsTotal;
  flash["fsUsed"] = m.fsUsed;

  JsonArray tasks = out["tasks"].to<JsonArray>();
  // Na pilha: esta função roda tanto na taskMetrics quanto na task do AsyncTCP.
  cpuusage::TaskInfo list[MAX_TRACKED_TASKS];
  const size_t count = cpuusage::tasks(list, MAX_TRACKED_TASKS);
  out["taskCount"] = count;
  for (size_t i = 0; i < count; i++) {
    JsonObject t = tasks.add<JsonObject>();
    t["n"] = list[i].name;
    t["c"] = list[i].core;
    t["p"] = list[i].priority;
    t["s"] = list[i].state;
    t["f"] = list[i].stackFree;
    t["u"] = list[i].cpuPct;
  }

  JsonArray prof = out["prof"].to<JsonArray>();
  for (uint8_t i = 0; i < static_cast<uint8_t>(ProfId::Count); i++) {
    const ProfId id = static_cast<ProfId>(i);
    const ProfStats st = profiler::get(id);
    JsonObject p = prof.add<JsonObject>();
    p["n"] = profiler::name(id);
    p["avg"] = st.calls ? static_cast<uint32_t>(st.totalUs / st.calls) : 0;
    p["min"] = st.minUs;
    p["max"] = st.maxUs;
    p["last"] = st.lastUs;
    p["calls"] = st.calls;
  }

  JsonObject sensorsOut = out["sensors"].to<JsonObject>();
  sensorsOut["slot1"] = s.slotOccupied[0];
  sensorsOut["slot2"] = s.slotOccupied[1];
  sensorsOut["nfcReads"] = shared::nfcReads.load();
  sensorsOut["nfcOk"] = nfc::ready();
  JsonArray edges = sensorsOut["edges"].to<JsonArray>();
  edges.add(sensors::edgeCount(SensorId::Slot1));
  edges.add(sensors::edgeCount(SensorId::Slot2));

  wifimgr::Status net;
  wifimgr::status(&net);
  JsonObject wifi = out["wifi"].to<JsonObject>();
  wifi["mode"] = wifimgr::modeName(net.mode);
  wifi["ssid"] = net.ssid;
  wifi["ip"] = net.ip;
  wifi["mac"] = net.mac;
  wifi["rssi"] = net.rssi;
  wifi["pct"] = signalPercent(net.rssi);
  wifi["bars"] = signalBars(net.rssi);
  wifi["channel"] = net.channel;
  wifi["sec"] = net.connectedSec;
  wifi["reconnects"] = net.reconnects;
  wifi["ap"] = net.apActive;
  wifi["apClients"] = net.apClients;

  JsonObject logs = out["logs"].to<JsonObject>();
  logs["count"] = storage::logs().count();
  logs["dropped"] = logger::dropped();

  JsonObject energy = out["power"].to<JsonObject>();
  energy["mode"] = power::ecoActive() ? "economico" : "normal";
  energy["modemSleep"] = cfg.modemSleep;
  energy["autoEco"] = cfg.autoEco;
  energy["wake"] = power::lastWake();
}

}  // namespace web
