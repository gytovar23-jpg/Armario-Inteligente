# Checklist de aceite do TDE

**Situação:** "Implementado" significa escrito e compilado. Nada foi executado em hardware real; a validação está em `docs/TESTES.md`.

| Requisito | Situação | Onde |
|---|---|---|
| Interface web embarcada | Implementado | `data/www/`, `src/web/server.cpp` |
| Performance: CPU | Implementado | `src/perf/cpu_usage.cpp` |
| Performance: heap, stack, flash, PSRAM | Implementado | `src/perf/memory.cpp`, `web::perfToJson` |
| Performance: tempo de ≥5 funções (7) | Implementado | `src/perf/profiler.*` |
| Performance: sensores, último valor e 24 h | Implementado | `src/tasks/task_sampler.cpp`, `/api/history/minutes` |
| Performance: tasks | Implementado | `cpuusage::tasks` |
| Performance: Wi-Fi/AP com sinal | Implementado | `src/web/wifi_mgr.cpp` |
| Performance: OTA | Implementado | `src/web/ota.cpp` |
| Logs ERROR/WARN/INFO, 1000 registros, data/hora | Implementado | `src/storage/logger.*`, `ring_file.*` |
| Exportação de logs CSV e JSON | Implementado | `routeLogs` em `src/web/api.cpp` |
| Configurar frequência de leitura | Implementado | `settings_model.*`, Admin → Configurações |
| Configurar parâmetros operacionais | Implementado | `settings_model.*` |
| Trocar Wi-Fi sem regravar | Implementado | `wifimgr::reloadSettings`, `/api/wifi/scan` |
| Aba Sobre | Implementado, **com placeholders a preencher** | `data/www/sobre.html`, `about.json` |
| Aba Changelog | Implementado | `data/www/changelog.html`, `changelog.json` |
| Histórico mínimo de 24 h | Implementado | anéis `min` e `hour` em `storage.cpp` |
| Nenhuma rotina bloqueante | Implementado; `delay()` só dentro de bibliotecas, equivalente a `vTaskDelay` | Busca em `src/` sem ocorrências |
| Mais de uma task, com divisão justificada | Implementado | `src/tasks/tasks.cpp`, `docs/ARQUITETURA.md` |
| Interrupções de hardware | Implementado | `onEdge` em `sensors.cpp`, `onSampleTick` em `task_sampler.cpp` |
| Light e Deep Sleep pela web | Implementado | `src/power/power.cpp` |
| ≥2 sensores de entrada | Implementado | 2 microswitches, NFC |
| ≥2 atuadores (sem LED/buzzer) | **Pendente de hardware**: o código do relé existe, falta o componente | `PIN_AUX_ACTUATOR`, `outputs::setAux` |
| Pronto para GitHub público | Implementado; sem credenciais reais, falta criar o repositório | `.gitignore`, `README.md` |

## Fora da checklist do prompt, também entregues

Modo diagnóstico (`/api/diag`), testes unitários (57), `docs/ARQUITETURA.md`, `docs/TESTES.md`, backup e restauração, watchdog nas tasks críticas, rollback de OTA.
