#ifdef SIMULATE_24H
#include "simulate_24h.h"

#include <Preferences.h>
#include <sys/time.h>

#include <algorithm>

#include "core/timebase.h"
#include "core/types.h"
#include "storage/logger.h"
#include "storage/storage.h"

namespace {
constexpr uint32_t DAY_SEC = 86400;
constexpr uint32_t MINUTES = DAY_SEC / 60;
constexpr size_t MAX_LOANS_PER_SLOT = 12;
constexpr size_t MAX_EVENTS = 2 * SLOT_COUNT * MAX_LOANS_PER_SLOT + 8;
constexpr uint16_t FIRST_USER_ID = 100;
constexpr uint8_t SESSION_UNLOCKED_PCT = 30;  // Janela de ~18 s destrancado por cartão.

const char* const NAMES[] = {"Ana Souza",   "Bruno Lima",   "Carla Mendes", "Diego Rocha",
                             "Eduarda Alves", "Gabriel Nunes", "Helena Prado", "Igor Martins"};
const char* const UIDS[] = {"1A2B3C4D", "2B3C4D5E", "3C4D5E6F", "4D5E6F70", "5E6F7081", "6F708192", "708192A3", "8192A3B4"};
constexpr uint8_t USER_COUNT = sizeof(NAMES) / sizeof(NAMES[0]);

struct Loan {
  uint32_t start;
  uint32_t end;
  uint8_t user;
};

Loan loans[SLOT_COUNT][MAX_LOANS_PER_SLOT];
size_t loanCount[SLOT_COUNT];
EventRecord events[MAX_EVENTS];
size_t eventCount = 0;
uint32_t seed = 0x2545F491;

uint32_t random(uint32_t n) {
  seed = seed * 1664525u + 1013904223u;
  return (seed >> 8) % n;
}

int localHour(uint32_t ts) {
  const time_t t = ts;
  struct tm local;
  localtime_r(&t, &local);
  return local.tm_hour;
}

// Empréstimos só entre 7 h e 22 h; ao fim das 24 h todo notebook está de volta,
// para bater com os microswitches apertados na bancada.
void planLoans(uint32_t from, uint32_t to) {
  for (uint8_t s = 0; s < SLOT_COUNT; s++) {
    loanCount[s] = 0;
    uint32_t t = from;
    while (loanCount[s] < MAX_LOANS_PER_SLOT && t < to) {
      t += 600 + random(5400);
      const int hour = localHour(t);
      if (hour < 7 || hour >= 22) continue;
      const uint32_t duration = 900 + random(9000);
      if (t + duration >= to - 300) break;
      loans[s][loanCount[s]++] = {t, t + duration, static_cast<uint8_t>(random(USER_COUNT))};
      t += duration;
    }
  }
}

void addEvent(uint32_t ts, EventType type, uint8_t slot, int user, uint32_t duration, const char* uid) {
  if (eventCount == MAX_EVENTS) return;
  EventRecord& e = events[eventCount++];
  memset(&e, 0, sizeof(e));
  e.ts = ts;
  e.durationSec = duration;
  e.type = static_cast<uint8_t>(type);
  e.slot = slot;
  e.flags = REC_TIME_SYNCED;
  e.userId = user >= 0 ? FIRST_USER_ID + user : USER_NONE;
  if (user >= 0) strlcpy(e.userName, NAMES[user], sizeof(e.userName));
  strlcpy(e.uid, uid, sizeof(e.uid));
}

void planEvents(uint32_t from, uint32_t to) {
  eventCount = 0;
  for (uint8_t s = 0; s < SLOT_COUNT; s++) {
    for (size_t i = 0; i < loanCount[s]; i++) {
      const Loan& l = loans[s][i];
      addEvent(l.start, EventType::Withdraw, s, l.user, 0, UIDS[l.user]);
      addEvent(l.end, EventType::Return, s, l.user, l.end - l.start, UIDS[l.user]);
    }
  }
  for (int i = 0; i < 4; i++) {
    uint32_t t = from + random(to - from);
    if (localHour(t) < 7) t += 8 * 3600;
    if (t < to) addEvent(t, EventType::Denied, 0xFF, -1, 0, "DEADBEEF");
  }
  std::sort(events, events + eventCount, [](const EventRecord& a, const EventRecord& b) { return a.ts < b.ts; });
}

bool onLoan(uint8_t slot, uint32_t ts) {
  for (size_t i = 0; i < loanCount[slot]; i++) {
    if (ts >= loans[slot][i].start && ts < loans[slot][i].end) return true;
  }
  return false;
}

uint16_t cardReadsBetween(uint32_t from, uint32_t to) {
  uint16_t n = 0;
  for (size_t i = 0; i < eventCount; i++) {
    if (events[i].ts >= from && events[i].ts < to) n++;
  }
  return n;
}

void writeHistory(uint32_t from) {
  RingFile& minutes = storage::minutes();
  RingFile& hours = storage::hours();
  uint32_t hourInUseSum = 0;
  for (uint32_t m = 0; m < MINUTES; m++) {
    const uint32_t ts = from + (m + 1) * 60;
    const uint16_t reads = cardReadsBetween(ts - 60, ts);
    uint8_t inUse = 0;
    MinuteSample sample{};
    sample.ts = ts;
    sample.flags = REC_TIME_SYNCED;
    const bool slot1Out = onLoan(0, ts);
    const bool slot2Out = onLoan(1, ts);
    inUse = slot1Out + slot2Out;
    sample.slot1Pct = slot1Out ? 0 : 100;
    sample.slot2Pct = slot2Out ? 0 : 100;
    sample.unlockedPct = reads ? std::min<uint32_t>(100, reads * SESSION_UNLOCKED_PCT) : 0;
    sample.cpu0Pct = 6 + random(8) + (reads ? 10 : 0);
    sample.cpu1Pct = 2 + random(4) + (reads ? 6 : 0);
    sample.rssi = 0;
    sample.heapFreeKb = 68 + random(10);
    sample.nfcReads = reads;
    minutes.append(&sample);

    hourInUseSum += inUse;
    if ((m + 1) % 60 == 0) {
      HourSample hour{};
      hour.ts = ts;
      hour.flags = REC_TIME_SYNCED;
      hour.inUseAvgX50 = hourInUseSum * 50 / 60;
      hour.minutes = 60;
      hours.append(&hour);
      hourInUseSum = 0;
      vTaskDelay(1);  // Deixa a IDLE rodar: a gravação toda leva alguns segundos.
    }
  }
  minutes.flush();
  hours.flush();
}

// Sem NTP no modo AP, o relógio parte da hora da compilação. Num reboot, avança
// até a última amostra gravada para os gráficos não voltarem no tempo.
uint32_t startClock() {
  uint32_t now = SIM_EPOCH;
  RingFile& minutes = storage::minutes();
  if (minutes.head() > 0) {
    MinuteSample last;
    RingFile::Reader reader(minutes);
    if (reader.read(minutes.head() - 1, &last) && (last.flags & REC_TIME_SYNCED)) now = std::max(now, last.ts + 60);
  }
  if (!timebase::synced()) {
    const timeval tv{static_cast<time_t>(now), 0};
    settimeofday(&tv, nullptr);
  }
  uint8_t flags = 0;
  return timebase::now(&flags);
}
}  // namespace

namespace devsim {

void run() {
  const uint32_t now = startClock();
  Preferences prefs;
  if (!prefs.begin("armario", false)) return;
  const bool seeded = prefs.getUInt("sim24", 0) == SIM_EPOCH;
  if (seeded) {
    prefs.end();
    return;
  }

  const uint32_t from = now - DAY_SEC;
  storage::events().clear();
  storage::minutes().clear();
  storage::hours().clear();
  planLoans(from, now);
  planEvents(from, now);
  for (size_t i = 0; i < eventCount; i++) storage::events().append(&events[i]);
  storage::events().flush();
  writeHistory(from);

  prefs.putUInt("sim24", SIM_EPOCH);
  prefs.end();
  LOG_WARN("sim", "Historico simulado de 24 h gravado: %u eventos", (unsigned)eventCount);
}

}  // namespace devsim
#endif
