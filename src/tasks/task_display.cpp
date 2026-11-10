#include <Arduino.h>
#include <time.h>

#include "core/shared_state.h"
#include "core/timebase.h"
#include "drivers/display.h"
#include "storage/logger.h"
#include "tasks.h"

namespace {
constexpr TickType_t PERIOD = pdMS_TO_TICKS(200);  // ~5 Hz: suficiente para texto.

void buildTitle(DisplayModel& m) {
  if (!timebase::synced()) {
    strlcpy(m.title, "Armario  --:--", sizeof(m.title));
    return;
  }
  const time_t now = time(nullptr);
  struct tm local;
  localtime_r(&now, &local);
  snprintf(m.title, sizeof(m.title), "Armario  %02d:%02d", local.tm_hour, local.tm_min);
}

const char* alarmText(AlarmReason r) {
  switch (r) {
    case AlarmReason::Violation:
      return "Slot violado";
    case AlarmReason::None:
      break;
  }
  return "";
}

void buildBody(const LockerSnapshot& s, DisplayModel& m) {
  if (s.noticeUntilMs != 0 && static_cast<int32_t>(millis() - s.noticeUntilMs) < 0) {
    strlcpy(m.line1, s.notice, sizeof(m.line1));
    return;
  }
  switch (s.state) {
    case LockerState::Idle:
      strlcpy(m.line1, "Aproxime o cartao", sizeof(m.line1));
      snprintf(m.line2, sizeof(m.line2), "S1:%s S2:%s", s.slotOccupied[0] ? "ocupado" : "livre",
               s.slotOccupied[1] ? "ocupado" : "livre");
      break;
    case LockerState::Open:
      strlcpy(m.line1, s.sessionUser, sizeof(m.line1));
      strlcpy(m.line2, "Retire ou devolva", sizeof(m.line2));
      break;
    case LockerState::Alarm:
      strlcpy(m.line1, s.alarmSilenced ? "Alarme silenciado" : "ALARME", sizeof(m.line1));
      strlcpy(m.line2, alarmText(s.alarm), sizeof(m.line2));
      break;
    case LockerState::Enroll:
      strlcpy(m.line1, "Modo cadastro", sizeof(m.line1));
      strlcpy(m.line2, "Aproxime o cartao", sizeof(m.line2));
      break;
  }
}
}  // namespace

void taskDisplay(void*) {
  if (!display::begin()) LOG_ERROR("display", "OLED nao responde");

  DisplayModel previous{};
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    const LockerSnapshot snapshot = shared::read();
    DisplayModel model{};
    buildTitle(model);
    buildBody(snapshot, model);
    strlcpy(model.footer, snapshot.networkLine, sizeof(model.footer));

    // Quadro idêntico não vai ao barramento: em repouso são 25 ms de I2C a
    // menos a cada 200 ms, e a medição de render reflete só trabalho real.
    if (memcmp(&model, &previous, sizeof(model)) != 0) {
      display::render(model);
      previous = model;
    }
    xTaskDelayUntil(&wake, PERIOD);
  }
}
