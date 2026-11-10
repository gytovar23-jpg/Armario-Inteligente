#pragma once
// Modelo das configurações e sua validação. Sem Arduino, para rodar nos testes.
#include <ArduinoJson.h>
#include <stdint.h>

#include "config.h"

struct Settings {
  uint32_t magic;
  uint16_t schema;

  // Frequência de leitura
  uint16_t sensorSampleMs;
  uint16_t nfcPollMs;

  // Operação do armário
  uint16_t unlockSec;      // Duração da sessão aberta pelo cartão.
  uint16_t relockDelaySec; // Prazo para trancar depois de cada mudança de slot.
  uint8_t servoLockedDeg;
  uint8_t servoUnlockedDeg;
  uint8_t beepVolume;   // 0–100: duty do PWM aplicado ao buzzer ativo.
  uint8_t beepPattern;  // 0 curto, 1 normal, 2 longo.
  bool hoursEnforced;
  uint8_t openHour;
  uint8_t closeHour;
  uint16_t maxLoanHours;
  uint16_t eventRetentionDays;

  // Rede
  char wifiSsid[33];
  char wifiPass[64];
  bool staticIp;
  char ip[16];
  char gateway[16];
  char subnet[16];
  char dns[16];
  char timezone[48];
  char ntpServer[48];

  // Energia
  uint16_t cpuMhz;
  bool modemSleep;
  bool autoEco;

  // Web
  bool publicPages;

  // Slots
  char slotAsset[SLOT_COUNT][24];
  bool slotMaintenance[SLOT_COUNT];
};

constexpr uint32_t SETTINGS_MAGIC = 0x41524D31;  // "ARM1"
constexpr uint16_t SETTINGS_SCHEMA = 2;  // 2: sem sensor de porta (saiu maxDoorOpenSec).

void settingsDefaults(Settings& s);

// Corrige valores fora de faixa vindos da NVS. Retorna true se o conteúdo não
// pôde ser reconhecido e os padrões foram restaurados.
bool settingsSanitize(Settings& s);

// Aplica um JSON parcial. É tudo ou nada: no erro, `s` fica intacto e `err`
// recebe o campo recusado.
bool settingsApplyJson(JsonObjectConst in, Settings& s, char* err, size_t errLen);

// A senha do Wi-Fi nunca sai pela API: includeSecrets só existe para testes.
void settingsToJson(const Settings& s, JsonObject out, bool includeSecrets = false);
