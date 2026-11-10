#include <Arduino.h>
#include <esp_heap_caps.h>

#include "core/shared_state.h"
#include "core/timebase.h"
#include "storage/aggregator.h"
#include "storage/settings.h"
#include "storage/storage.h"
#include "tasks.h"

namespace {
constexpr uint32_t TIMER_HZ = 1000000;
constexpr uint8_t MINUTES_PER_HOUR = 60;

TaskHandle_t samplerHandle;
hw_timer_t* timer;

// Interrupção de timer de hardware: o período não sofre jitter do escalonador.
void IRAM_ATTR onSampleTick() {
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(samplerHandle, &woken);
  if (woken) portYIELD_FROM_ISR();
}

void programTimer(uint16_t periodMs) { timerAlarm(timer, uint64_t(periodMs) * 1000, true, 0); }

void postMinute(const MinuteResult& r, uint16_t nfcReadsInMinute) {
  StorageMsg msg;
  msg.kind = StorageMsg::Minute;
  MinuteSample& m = msg.minute;
  m.flags = 0;
  m.ts = timebase::now(&m.flags);
  m.unlockedPct = r.unlockedPct;
  m.slot1Pct = r.slot1Pct;
  m.slot2Pct = r.slot2Pct;
  m.cpu0Pct = r.cpu0Pct;
  m.cpu1Pct = r.cpu1Pct;
  m.rssi = r.rssi;
  m.reserved = 0;
  m.heapFreeKb = r.heapMinKb;
  m.nfcReads = nfcReadsInMinute;
  storage::post(msg, pdMS_TO_TICKS(100));
}

void postHour(uint8_t avg, uint16_t minutes) {
  StorageMsg msg;
  msg.kind = StorageMsg::Hour;
  HourSample& h = msg.hour;
  h.flags = 0;
  h.ts = timebase::now(&h.flags);
  h.inUseAvgX50 = avg;
  h.minutes = minutes;
  storage::post(msg, pdMS_TO_TICKS(100));
}

// Notebooks fora do armário, descontando slots em manutenção.
uint8_t notebooksInUse(const LockerSnapshot& s, const Settings& cfg) {
  uint8_t inUse = 0;
  for (uint8_t i = 0; i < SLOT_COUNT; i++) {
    if (!s.slotOccupied[i] && !cfg.slotMaintenance[i]) inUse++;
  }
  return inUse;
}
}  // namespace

void taskSampler(void*) {
  samplerHandle = xTaskGetCurrentTaskHandle();
  timer = timerBegin(TIMER_HZ);
  timerAttachInterrupt(timer, onSampleTick);

  Settings cfg = settings::get();
  uint32_t seenGen = settings::generation();
  programTimer(cfg.sensorSampleMs);

  MinuteAggregator minute;
  HourAggregator hour;
  uint32_t minuteStart = millis();
  uint32_t nfcAtMinuteStart = shared::nfcReads.load();
  uint32_t inUseSum = 0;
  uint32_t inUseCount = 0;

  for (;;) {
    // O timeout evita ficar parado para sempre se o timer for reprogramado.
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(65000)) == 0) continue;

    if (settings::generation() != seenGen) {
      seenGen = settings::generation();
      const uint16_t previous = cfg.sensorSampleMs;
      cfg = settings::get();
      if (cfg.sensorSampleMs != previous) programTimer(cfg.sensorSampleMs);
    }

    const LockerSnapshot s = shared::read();
    RawSample sample{};
    sample.unlocked = !s.locked;
    sample.slot1 = s.slotOccupied[0];
    sample.slot2 = s.slotOccupied[1];
    sample.cpu0 = shared::cpuPct0.load();
    sample.cpu1 = shared::cpuPct1.load();
    sample.heapFreeKb = heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024;
    sample.rssi = shared::wifiRssi.load();
    minute.add(sample);
    inUseSum += notebooksInUse(s, cfg);
    inUseCount++;

    if (millis() - minuteStart < 60000) continue;
    minuteStart += 60000;
    // Depois de um light sleep ficaríamos para trás: realinha em vez de gerar rajada.
    if (millis() - minuteStart >= 60000) minuteStart = millis();

    const uint32_t nfcNow = shared::nfcReads.load();
    postMinute(minute.finish(), nfcNow - nfcAtMinuteStart);
    nfcAtMinuteStart = nfcNow;

    hour.addMinute(inUseSum * 50 / inUseCount);
    inUseSum = 0;
    inUseCount = 0;
    if (hour.minutes() >= MINUTES_PER_HOUR) {
      const uint16_t minutes = hour.minutes();
      postHour(hour.finish(), minutes);
    }
  }
}
