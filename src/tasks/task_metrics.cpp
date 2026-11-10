#include <Arduino.h>
#include <ArduinoJson.h>

#include "config.h"
#include "perf/cpu_usage.h"
#include "perf/memory.h"
#include "tasks.h"
#include "web/json_views.h"
#include "web/server.h"

namespace {
char buffer[6144];  // Comporta ~30 tasks; estático para não disputar o heap.

// DIAGNOSTICO TEMPORARIO: heap e pilhas no serial a cada 5 s.
void diagHeap() {
  static uint32_t tick = 0;
  if (++tick % 5) return;
  const bool ok = heap_caps_check_integrity_all(true);
  Serial.printf("[DIAG] heap livre %u, min %u, maior bloco %u, integro %s\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), ok ? "sim" : "NAO");
  static TaskStatus_t st[32];
  const UBaseType_t n = uxTaskGetSystemState(st, 32, nullptr);
  for (UBaseType_t i = 0; i < n; i++) {
    if (st[i].usStackHighWaterMark < 768) {
      Serial.printf("[DIAG] pilha baixa: %s sobra %u\n", st[i].pcTaskName, (unsigned)st[i].usStackHighWaterMark);
    }
  }
}
}

void taskMetrics(void*) {
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    xTaskDelayUntil(&wake, pdMS_TO_TICKS(METRICS_PERIOD_MS));
    cpuusage::sample();
    perfmem::sample();
    diagHeap();

    // Sem ninguém olhando não há por que serializar: medir não pode virar carga.
    if (!web::hasClients()) continue;
    JsonDocument doc;
    doc["t"] = "perf";
    web::perfToJson(doc.as<JsonObject>());
    const size_t len = serializeJson(doc, buffer, sizeof(buffer));
    if (len > 0 && len < sizeof(buffer)) web::broadcast(buffer, len);
  }
}
