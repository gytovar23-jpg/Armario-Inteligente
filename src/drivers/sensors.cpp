#include "sensors.h"

#include <Arduino.h>
#include <esp_timer.h>

#include "config.h"
#include "pins.h"

namespace {
constexpr size_t COUNT = static_cast<size_t>(SensorId::Count);
const uint8_t PINS[COUNT] = {PIN_SLOT_1, PIN_SLOT_2};

QueueHandle_t sensorQueue;
esp_timer_handle_t debounceTimers[COUNT];
bool stable[COUNT];
volatile uint32_t edges[COUNT];

// Com pull-up, microswitch pressionado = LOW.
bool isActive(SensorId id) { return !digitalRead(PINS[static_cast<size_t>(id)]); }

// ISR mínima: conta, enfileira e sai. Nada de lógica: o tempo aqui é latência
// imposta a todas as outras interrupções do core.
void IRAM_ATTR onEdge(void* arg) {
  const uintptr_t index = reinterpret_cast<uintptr_t>(arg);
  edges[index] = edges[index] + 1;
  SensorMsg msg{SensorMsg::Edge, static_cast<SensorId>(index)};
  BaseType_t woken = pdFALSE;
  xQueueSendFromISR(sensorQueue, &msg, &woken);
  if (woken) portYIELD_FROM_ISR();
}

void onDebounceExpired(void* arg) {
  SensorMsg msg{SensorMsg::Settled, static_cast<SensorId>(reinterpret_cast<uintptr_t>(arg))};
  xQueueSend(sensorQueue, &msg, 0);
}
}  // namespace

namespace sensors {

bool begin(QueueHandle_t queue) {
  sensorQueue = queue;
  for (size_t i = 0; i < COUNT; i++) {
    pinMode(PINS[i], INPUT_PULLUP);
    stable[i] = isActive(static_cast<SensorId>(i));
    esp_timer_create_args_t args{};
    args.callback = onDebounceExpired;
    args.arg = reinterpret_cast<void*>(i);
    args.name = "debounce";
    if (esp_timer_create(&args, &debounceTimers[i]) != ESP_OK) return false;
    attachInterruptArg(PINS[i], onEdge, reinterpret_cast<void*>(i), CHANGE);
  }
  return true;
}

void process(const SensorMsg& msg, SensorChange* out, bool* changed) {
  *changed = false;
  const size_t i = static_cast<size_t>(msg.id);
  if (msg.kind == SensorMsg::Edge) {
    // Cada nova borda adia a confirmação: a janela só fecha quando o sinal para.
    esp_timer_stop(debounceTimers[i]);
    esp_timer_start_once(debounceTimers[i], DEBOUNCE_MS * 1000);
    return;
  }
  const bool level = isActive(msg.id);
  if (level == stable[i]) return;
  stable[i] = level;
  *out = {msg.id, level};
  *changed = true;
}

void suspendInterrupts() {
  for (size_t i = 0; i < COUNT; i++) detachInterrupt(PINS[i]);
}

void resumeInterrupts() {
  for (size_t i = 0; i < COUNT; i++) attachInterruptArg(PINS[i], onEdge, reinterpret_cast<void*>(i), CHANGE);
}

bool active(SensorId id) { return stable[static_cast<size_t>(id)]; }

uint32_t edgeCount(SensorId id) { return edges[static_cast<size_t>(id)]; }

void resync() {
  for (size_t i = 0; i < COUNT; i++) {
    const SensorId id = static_cast<SensorId>(i);
    if (isActive(id) == stable[i]) continue;
    SensorMsg msg{SensorMsg::Edge, id};
    xQueueSend(sensorQueue, &msg, 0);
  }
}

}  // namespace sensors
