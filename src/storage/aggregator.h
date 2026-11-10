#pragma once
// Redução de amostras em médias de 1 min e 1 h. Sem Arduino, para os testes.
#include <stdint.h>

struct RawSample {
  bool unlocked;  // Tranca solta: sem sensor de porta, é o que indica armário aberto.
  bool slot1;
  bool slot2;
  uint8_t cpu0;
  uint8_t cpu1;
  uint16_t heapFreeKb;
  int8_t rssi;
};

struct MinuteResult {
  uint8_t unlockedPct;
  uint8_t slot1Pct;
  uint8_t slot2Pct;
  uint8_t cpu0Pct;
  uint8_t cpu1Pct;
  uint16_t heapMinKb;  // Mínimo e não média: o que importa é o pior momento.
  int8_t rssi;
};

class MinuteAggregator {
 public:
  void add(const RawSample& s);
  bool empty() const { return samples_ == 0; }
  MinuteResult finish();  // Devolve o agregado e zera para o próximo minuto.

 private:
  uint32_t samples_ = 0;
  uint32_t unlocked_ = 0;
  uint32_t slot1_ = 0;
  uint32_t slot2_ = 0;
  uint32_t cpu0_ = 0;
  uint32_t cpu1_ = 0;
  int32_t rssi_ = 0;
  uint16_t heapMin_ = 0xFFFF;
};

class HourAggregator {
 public:
  void addMinute(uint8_t notebooksInUseX50);
  uint16_t minutes() const { return minutes_; }
  uint8_t finish();

 private:
  uint32_t sum_ = 0;
  uint16_t minutes_ = 0;
};
