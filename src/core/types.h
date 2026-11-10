#pragma once
// Tipos gravados em flash ou trocados entre tasks. Não depende de Arduino para
// poder ser incluído pelos testes no PC.
#include <stdint.h>

#include "config.h"

enum class LogLevel : uint8_t { Error = 0, Warn = 1, Info = 2 };

// Bit em flags de todo registro: a data veio do NTP. Sem ele, ts é uptime em s.
constexpr uint8_t REC_TIME_SYNCED = 0x01;

struct __attribute__((packed)) LogRecord {
  uint32_t ts;
  uint8_t level;
  uint8_t flags;
  char module[10];
  char msg[80];
};
static_assert(sizeof(LogRecord) == 96, "tamanho fixo de registro em flash");

enum class EventType : uint8_t {
  Withdraw = 0,   // Retirada
  Return = 1,     // Devolução
  Denied = 2,     // Cartão recusado
  Violation = 3,  // Slot mudou sem sessão
  Intrusion = 4,  // Só em registros antigos: exigia sensor de porta.
};

constexpr uint16_t USER_NONE = 0xFFFF;
constexpr uint16_t USER_WEB_ADMIN = 0xFFFE;  // Ação pela web, sem cartão.

struct __attribute__((packed)) EventRecord {
  uint32_t ts;
  uint32_t durationSec;  // Só na devolução: duração do empréstimo.
  uint16_t userId;
  uint8_t type;
  uint8_t slot;  // 0 ou 1; 0xFF quando o evento não envolve slot.
  uint8_t flags;
  char userName[24];
  char uid[15];  // Hexadecimal; MIFARE Classic tem 4 bytes, UID longo tem 7.
};
static_assert(sizeof(EventRecord) == 52, "tamanho fixo de registro em flash");

// Agregado de 1 minuto: alimenta os gráficos de 24 h de sensores e desempenho.
struct __attribute__((packed)) MinuteSample {
  uint32_t ts;
  uint8_t flags;
  uint8_t unlockedPct;  // % do minuto com a tranca solta.
  uint8_t slot1Pct;  // % do minuto com slot ocupado.
  uint8_t slot2Pct;
  uint8_t cpu0Pct;
  uint8_t cpu1Pct;
  int8_t rssi;
  uint8_t reserved;
  uint16_t heapFreeKb;
  uint16_t nfcReads;
};
static_assert(sizeof(MinuteSample) == 16, "tamanho fixo de registro em flash");

// Agregado de 1 hora: "notebooks em uso" nos últimos 7 dias.
struct __attribute__((packed)) HourSample {
  uint32_t ts;
  uint8_t flags;
  uint8_t inUseAvgX50;  // Média × 50: guarda 0,00 a 5,00 notebooks em 1 byte.
  uint16_t minutes;
};
static_assert(sizeof(HourSample) == 8, "tamanho fixo de registro em flash");
