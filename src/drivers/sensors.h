#pragma once
// Microswitches dos slots: borda na ISR, debounce com esp_timer, decisão na task.
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdint.h>

enum class SensorId : uint8_t { Slot1 = 0, Slot2 = 1, Count = 2 };

struct SensorMsg {
  enum Kind : uint8_t {
    Edge,     // Da ISR: houve borda, (re)inicia a janela de debounce.
    Settled,  // Do timer: a janela terminou sem novas bordas, pode ler o nível.
  } kind;
  SensorId id;
};

struct SensorChange {
  SensorId id;
  bool active;  // Slot ocupado (microswitch pressionado).
};

namespace sensors {

bool begin(QueueHandle_t queue);
void process(const SensorMsg& msg, SensorChange* out, bool* changed);

bool active(SensorId id);       // Último valor estável.
uint32_t edgeCount(SensorId id); // Total de interrupções (evidência do debounce).

// Reavalia os níveis sem depender de borda. Cobre fila cheia e o retorno do
// light sleep, quando a ISR pode não ter sido atendida.
void resync();

// O wake-up do light sleep reprograma as interrupções dos slots; estas duas as
// suspendem antes de dormir e as devolvem ao modo CHANGE ao acordar.
void suspendInterrupts();
void resumeInterrupts();

}  // namespace sensors
