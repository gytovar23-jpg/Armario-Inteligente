#include "aggregator.h"

namespace {
uint8_t percent(uint32_t part, uint32_t total) { return (part * 100 + total / 2) / total; }
}  // namespace

void MinuteAggregator::add(const RawSample& s) {
  samples_++;
  unlocked_ += s.unlocked;
  slot1_ += s.slot1;
  slot2_ += s.slot2;
  cpu0_ += s.cpu0;
  cpu1_ += s.cpu1;
  rssi_ += s.rssi;
  if (s.heapFreeKb < heapMin_) heapMin_ = s.heapFreeKb;
}

MinuteResult MinuteAggregator::finish() {
  MinuteResult r{};
  if (samples_ > 0) {
    r.unlockedPct = percent(unlocked_, samples_);
    r.slot1Pct = percent(slot1_, samples_);
    r.slot2Pct = percent(slot2_, samples_);
    r.cpu0Pct = (cpu0_ + samples_ / 2) / samples_;
    r.cpu1Pct = (cpu1_ + samples_ / 2) / samples_;
    r.rssi = rssi_ / int32_t(samples_);
    r.heapMinKb = heapMin_;
  }
  *this = MinuteAggregator();
  return r;
}

void HourAggregator::addMinute(uint8_t notebooksInUseX50) {
  sum_ += notebooksInUseX50;
  minutes_++;
}

uint8_t HourAggregator::finish() {
  const uint8_t avg = minutes_ ? (sum_ + minutes_ / 2) / minutes_ : 0;
  *this = HourAggregator();
  return avg;
}
