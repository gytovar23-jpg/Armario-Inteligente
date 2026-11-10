#include "buzzer_led.h"

#include <Arduino.h>
#include <driver/rtc_io.h>

#include "config.h"
#include "pins.h"

namespace {
// O buzzer é ativo: tem oscilador próprio, então o PWM só recorta o som. É o
// único "volume" possível neste hardware, e o efeito precisa ser ajustado na bancada.
constexpr uint32_t BUZZER_PWM_HZ = 2000;
constexpr uint8_t BUZZER_PWM_BITS = 8;
bool auxOn = false;
}  // namespace

namespace outputs {

void begin() {
  // Devolve o pino ao GPIO digital caso o deep sleep o tenha deixado no domínio RTC.
  rtc_gpio_deinit(static_cast<gpio_num_t>(PIN_BUZZER));
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_RED, LOW);
  ledcAttach(PIN_BUZZER, BUZZER_PWM_HZ, BUZZER_PWM_BITS);
  ledcWrite(PIN_BUZZER, 0);
#if AUX_ACTUATOR_ENABLED
  pinMode(PIN_AUX_ACTUATOR, OUTPUT);
  digitalWrite(PIN_AUX_ACTUATOR, !AUX_ACTIVE_LEVEL);
#endif
}

void setGreen(bool on) { digitalWrite(PIN_LED_GREEN, on); }

void setRed(bool on) { digitalWrite(PIN_LED_RED, on); }

void setBuzzer(bool on, uint8_t volumePct) {
  const uint32_t maxDuty = (1u << BUZZER_PWM_BITS) - 1;
  ledcWrite(PIN_BUZZER, on ? maxDuty * volumePct / 100 : 0);
}

void setAux(bool on) {
  auxOn = on;
#if AUX_ACTUATOR_ENABLED
  digitalWrite(PIN_AUX_ACTUATOR, on ? AUX_ACTIVE_LEVEL : !AUX_ACTIVE_LEVEL);
#endif
}

bool auxState() { return auxOn; }

void prepareForDeepSleep() {
  ledcWrite(PIN_BUZZER, 0);
  ledcDetach(PIN_BUZZER);
  // O pull-down RTC segura a base do BC547 em baixo: o buzzer fica mudo dormindo.
  const gpio_num_t buzzer = static_cast<gpio_num_t>(PIN_BUZZER);
  rtc_gpio_init(buzzer);
  rtc_gpio_set_direction(buzzer, RTC_GPIO_MODE_DISABLED);
  rtc_gpio_pulldown_en(buzzer);
  rtc_gpio_pullup_dis(buzzer);
}

}  // namespace outputs
