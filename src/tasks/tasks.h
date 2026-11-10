#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace tasks {

// Cria todas as tasks fixadas em seus cores. Falha de criação é fatal: sem as
// tasks o armário não opera com segurança.
void startAll();

// Registra a task atual no watchdog com o timeout do projeto.
void watchThisTask();

}  // namespace tasks

void taskNFC(void* arg);
void taskAccess(void* arg);
void taskActuators(void* arg);
void taskDisplay(void* arg);
void taskWeb(void* arg);
void taskStorage(void* arg);
void taskMetrics(void* arg);
void taskSampler(void* arg);
void taskPower(void* arg);
