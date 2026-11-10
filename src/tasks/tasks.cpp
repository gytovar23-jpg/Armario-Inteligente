#include "tasks.h"

#include <Arduino.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "storage/storage.h"

namespace {
// Prioridades: núcleo físico acima do Wi-Fi do usuário e abaixo do esp_timer (22),
// para o debounce nunca esperar o nosso código.
//
// O core 1 fica para a interação física porque o core 0 hospeda a pilha Wi-Fi
// (prioridade 23) e o lwIP: ali, uma rajada de pacotes atrasaria um servo.
//
// Pilhas: valores iniciais folgados, a validar pelo high water mark da página
// Performance. Sem PSRAM, cada KB a mais de pilha é um KB a menos para o web.
struct TaskSpec {
  TaskFunction_t fn;
  const char* name;
  uint32_t stack;
  UBaseType_t priority;
  BaseType_t core;
  void* arg;
};

void createAll() {
  const TaskSpec specs[] = {
      // Access acima de NFC: decidir sobre um evento já recebido vem antes de buscar outro.
      {taskAccess, "taskAccess", 5120, 6, 1, nullptr},
      // NFC dorme em vTaskDelay enquanto espera o RF, então não prende o core.
      {taskNFC, "taskNFC", 4096, 5, 1, nullptr},
      // Precisa de ciclo estável de 10 ms para os padrões de bipe, mas só faz GPIO.
      {taskActuators, "taskActuators", 3072, 4, 1, nullptr},
      // Prioridade mínima útil: ~25 ms de I2C não podem atrasar a tranca.
      {taskDisplay, "taskDisplay", 3584, 1, 1, nullptr},
      // Acima de storage/métricas para reconexão e OTA não esperarem a flash.
      {taskWeb, "taskWeb", 6144, 3, 0, nullptr},
      // Amostragem periódica: leve, e o atraso distorceria o histórico.
      {taskSampler, "taskSampler", 3072, 2, 0, nullptr},
      // Bloqueia em flash; rodar na mínima deixa a rede fluir durante as escritas.
      {taskStorage, "taskStorage", 6144, 1, 0, nullptr},
      // JSON de métricas é o maior consumidor de pilha depois da web.
      {taskMetrics, "taskMetrics", 6144, 1, 0, nullptr},
      // Passa a vida bloqueada; só age sob pedido e precisa do Wi-Fi quieto.
      {taskPower, "taskPower", 4096, 2, 0, nullptr},
  };

  for (const TaskSpec& t : specs) {
    if (xTaskCreatePinnedToCore(t.fn, t.name, t.stack, t.arg, t.priority, nullptr, t.core) != pdPASS) {
      Serial.printf("Falha ao criar %s\n", t.name);
      abort();
    }
  }
}
}  // namespace

namespace tasks {

void startAll() {
  esp_task_wdt_config_t wdt{};
  wdt.timeout_ms = WDT_TIMEOUT_S * 1000;
  wdt.idle_core_mask = 1 << 0;  // Mantém o idle do core 0, como o Arduino configura.
  wdt.trigger_panic = true;     // Travou: reinicia, e o motivo aparece na Performance.
  esp_task_wdt_reconfigure(&wdt);

  createAll();
}

void watchThisTask() { esp_task_wdt_add(nullptr); }

}  // namespace tasks

void taskStorage(void*) { storage::runWriter(); }
