#pragma once
// Canais entre tasks e a fotografia do armário. É o único estado compartilhado
// do sistema, sempre atrás de fila ou mutex.
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <atomic>

#include "drivers/sensors.h"
#include "state_machine.h"

struct UidMsg {
  uint8_t uid[7];
  uint8_t len;
};

struct AccessCommand {
  enum Kind : uint8_t { Unlock, Lock, Silence, EnrollStart, EnrollCancel } kind;
  uint16_t userId;
};

enum class FeedbackKind : uint8_t { Granted, Denied, EnrollDone, AlarmOn, AlarmOff };

struct ActuatorCmd {
  enum Kind : uint8_t { Lock, Unlock, Feedback, Aux, TestServo, TestBuzzer, TestGreen, TestRed, Sleep } kind;
  uint8_t arg;
};

struct ActiveLoan {
  bool active;
  uint8_t flags;  // REC_TIME_SYNCED: `since` é epoch; senão, uptime.
  uint16_t userId;
  uint32_t since;
  char userName[24];
};

struct LockerSnapshot {
  LockerState state;
  AlarmReason alarm;
  bool alarmSilenced;
  bool locked;
  bool slotOccupied[SLOT_COUNT];
  ActiveLoan loans[SLOT_COUNT];
  bool overdue[SLOT_COUNT];
  char sessionUser[24];
  char notice[22];         // Mensagem transitória do OLED.
  uint32_t noticeUntilMs;
  char lastUid[15];        // Último cartão lido; o modo cadastro entrega por aqui.
  uint32_t enrollCaptureId; // Cresce a cada captura, para a web notar a novidade.
  char networkLine[22];    // Rodapé do OLED, escrito pela taskWeb.
};

namespace shared {

extern QueueHandle_t uidQueue;
extern QueueHandle_t sensorQueue;
extern QueueHandle_t commandQueue;
extern QueueHandle_t actuatorQueue;
extern QueueSetHandle_t accessSet;

// Contadores simples: atômicos dispensam mutex e não geram contenção entre cores.
extern std::atomic<uint32_t> nfcReads;
extern std::atomic<uint32_t> nfcFailures;
extern std::atomic<uint32_t> stateVersion;  // Cresce quando a fotografia muda.
extern std::atomic<uint8_t> cpuPct0;
extern std::atomic<uint8_t> cpuPct1;
extern std::atomic<int8_t> wifiRssi;

bool begin();

LockerSnapshot read();

// Aplica `fn` à fotografia sob o mutex. Seção curta: só cópia de campos.
template <typename Fn>
void update(Fn fn);

void lock();
void unlock();
LockerSnapshot& raw();

bool sendActuator(ActuatorCmd::Kind kind, uint8_t arg = 0);
bool sendCommand(AccessCommand::Kind kind, uint16_t userId = USER_NONE);

}  // namespace shared

template <typename Fn>
void shared::update(Fn fn) {
  lock();
  fn(raw());
  unlock();
  stateVersion++;
}
