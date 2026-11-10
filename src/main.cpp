// Ponto de entrada: inicializa na ordem das dependências, cria as tasks e sai.
// O loop() do Arduino não é usado: todo o trabalho acontece em tasks FreeRTOS.
#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include "core/shared_state.h"
#include "core/usage_store.h"
#include "dev/simulate_24h.h"
#include "perf/cpu_usage.h"
#include "perf/memory.h"
#include "power/power.h"
#include "storage/logger.h"
#include "storage/settings.h"
#include "storage/storage.h"
#include "storage/users_db.h"
#include "tasks/tasks.h"
#include "version.h"

namespace {
const char* wakeCauseText(esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
      return "timer";
    default:
      return nullptr;
  }
}
}  // namespace

void setup() {
  // Buffer maior: logs no serial não devem bloquear tasks do core físico.
  Serial.setTxBufferSize(1024);
  Serial.begin(115200);

  if (!shared::begin()) abort();  // Sem filas não há como operar.
  const bool storageOk = storage::begin();
  settings::begin();
  // Antes de refazer as estatísticas: a hora local delas depende do fuso.
  setenv("TZ", settings::get().timezone, 1);
  tzset();
  const bool usersOk = users::begin();
#ifdef SIMULATE_24H
  devsim::run();
#endif
  usage::begin();

  power::begin();
  cpuusage::begin();
  perfmem::begin();
  const esp_sleep_wakeup_cause_t wake = esp_sleep_get_wakeup_cause();
  tasks::startAll();

  LOG_INFO("boot", "Firmware %s, reset %d", FW_VERSION, (int)esp_reset_reason());
  if (const char* text = wakeCauseText(wake)) {
    LOG_INFO("boot", "Acordou do deep sleep por %s", text);
    power::setBootWake(text);
  }
  if (!storageOk) LOG_ERROR("boot", "LittleFS indisponivel; dados nao serao persistidos");
  if (!usersOk) LOG_ERROR("boot", "Cadastro de usuarios nao carregado");

  vTaskDelete(nullptr);
}

void loop() {}
