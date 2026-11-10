// Gerenciamento de energia. Fica numa task própria porque entrar em sono exige
// esperar o flush e o desligamento do rádio, o que não cabe num handler web.
#include "power.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include <atomic>

#include "core/access_control.h"
#include "core/shared_state.h"
#include "drivers/sensors.h"
#include "pins.h"
#include "storage/logger.h"
#include "storage/settings.h"
#include "storage/storage.h"
#include "web/server.h"
#include "web/wifi_mgr.h"

namespace {
constexpr uint32_t ECO_CHECK_MS = 60000;
constexpr uint32_t RESPONSE_GRACE_MS = 800;  // A resposta HTTP precisa sair antes do rádio cair.
constexpr uint32_t MIN_SLEEP_S = 5;
constexpr uint32_t MAX_SLEEP_S = 86400;
constexpr uint16_t ECO_MHZ = 80;  // Mínimo que mantém o Wi-Fi operando.

struct SleepRequest {
  power::Request kind;
  uint32_t seconds;
};

QueueHandle_t requests;
std::atomic<bool> eco{false};
char wakeText[28];

void setWake(const char* text) { strlcpy(wakeText, text, sizeof(wakeText)); }

void prepare(const SleepRequest& r, bool deep) {
  vTaskDelay(pdMS_TO_TICKS(RESPONSE_GRACE_MS));
  LOG_INFO("power", "Entrando em %s por %lu s", deep ? "deep sleep" : "light sleep", (unsigned long)r.seconds);
  shared::sendActuator(ActuatorCmd::Sleep, deep);
  // Registrar antes de dormir é requisito: o flush garante que o log sobreviva.
  storage::flushAndWait(pdMS_TO_TICKS(2000));
  if (!wifimgr::pause(3000)) LOG_WARN("power", "Wi-Fi nao desligou a tempo");
  Serial.flush();
}

const gpio_num_t SLOT_PINS[] = {static_cast<gpio_num_t>(PIN_SLOT_1), static_cast<gpio_num_t>(PIN_SLOT_2)};

void enterLightSleep(const SleepRequest& r) {
  prepare(r, false);

  sensors::suspendInterrupts();
  esp_sleep_enable_timer_wakeup(uint64_t(r.seconds) * 1000000);
  // O wake-up por GPIO é por nível: acorda no nível oposto ao atual, ou seja,
  // quando qualquer slot mudar. Mexer num slot dormindo vira violação pela FSM.
  for (gpio_num_t pin : SLOT_PINS) {
    gpio_wakeup_enable(pin, gpio_get_level(pin) ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
  }
  esp_sleep_enable_gpio_wakeup();

  esp_light_sleep_start();  // RAM e tasks preservadas; retoma nesta linha.

  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  for (gpio_num_t pin : SLOT_PINS) gpio_wakeup_disable(pin);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  sensors::resumeInterrupts();
  sensors::resync();

  const char* text = cause == ESP_SLEEP_WAKEUP_TIMER ? "timer" : cause == ESP_SLEEP_WAKEUP_GPIO ? "slot" : "outro";
  setWake(text);
  wifimgr::resume();
  LOG_INFO("power", "Acordou do light sleep por %s", text);
}

void enterDeepSleep(const SleepRequest& r) {
  prepare(r, true);

  // Só o timer acorda: os slots (GPIO 16/17) não são RTC GPIO e não servem de
  // fonte de wake-up em deep sleep. Limitação conhecida: um slot mexido durante
  // o deep sleep não vira violação, porque a FSM parte do estado lido no boot.
  sensors::suspendInterrupts();
  esp_sleep_enable_timer_wakeup(uint64_t(r.seconds) * 1000000);

  esp_deep_sleep_start();  // Não retorna: ao acordar o firmware reinicia.
}

void applyFrequency() {
  const Settings cfg = settings::get();
  const bool wantEco = cfg.autoEco && !accessctl::insideWindow(cfg) && shared::read().state == LockerState::Idle;
  const uint16_t target = wantEco ? ECO_MHZ : cfg.cpuMhz;
  if (wantEco != eco.load()) LOG_INFO("power", "Modo economico %s", wantEco ? "ligado" : "desligado");
  eco = wantEco;
  if (getCpuFrequencyMhz() != target) {
    setCpuFrequencyMhz(target);
    LOG_INFO("power", "CPU em %u MHz", target);
  }
}
}  // namespace

namespace power {

void begin() {
  requests = xQueueCreate(1, sizeof(SleepRequest));
}

bool request(Request kind, uint32_t seconds, char* reason, size_t reasonLen) {
  const LockerSnapshot s = shared::read();
  if (s.state != LockerState::Idle) {
    strlcpy(reason, "armario em uso", reasonLen);
    return false;
  }
  if (web::otaInProgress()) {
    strlcpy(reason, "atualizacao em andamento", reasonLen);
    return false;
  }
  if (seconds < MIN_SLEEP_S || seconds > MAX_SLEEP_S) {
    strlcpy(reason, "tempo fora da faixa", reasonLen);
    return false;
  }
  SleepRequest r{kind, seconds};
  if (xQueueSend(requests, &r, 0) != pdTRUE) {
    strlcpy(reason, "ja existe pedido pendente", reasonLen);
    return false;
  }
  return true;
}

void run() {
  for (;;) {
    SleepRequest r;
    if (xQueueReceive(requests, &r, pdMS_TO_TICKS(ECO_CHECK_MS)) == pdTRUE) {
      if (r.kind == Request::LightSleep) {
        enterLightSleep(r);
      } else {
        enterDeepSleep(r);
      }
    }
    applyFrequency();
  }
}

bool ecoActive() { return eco.load(); }

const char* lastWake() { return wakeText; }

void setBootWake(const char* text) { setWake(text); }

}  // namespace power
