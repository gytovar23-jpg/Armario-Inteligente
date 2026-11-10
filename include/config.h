#pragma once
// Limites de compilação e valores padrão. O que o administrador pode mudar em
// tempo de execução fica em storage/settings_model.h; aqui fica o que define
// tamanho de memória e de arquivos.

// ---- Domínio ---------------------------------------------------------------
#define SLOT_COUNT 2
#define MAX_USERS 64  // ~100 B por usuário em RAM: 6,4 KB de um heap sem PSRAM.

// ---- Segundo atuador (pendência de hardware) --------------------------------
// 1 = relé de iluminação/cooler interno ligado durante a sessão (tranca solta).
#define AUX_ACTUATOR_ENABLED 1
#define AUX_ACTIVE_LEVEL HIGH

// ---- Temporização ----------------------------------------------------------
#define DEBOUNCE_MS 30           // Microswitch quica por até ~10 ms.
#define NFC_REPEAT_SUPPRESS_MS 2000  // Cartão parado no leitor não repete leitura.
#define NFC_READ_TIMEOUT_MS 60   // Curto para não prender a task no RF.
#define ENROLL_TIMEOUT_MS 30000
#define WDT_TIMEOUT_S 8          // Acima do pior caso de escrita em flash + OLED.

// ---- Filas -----------------------------------------------------------------
// Dimensionadas para o pico: uma rajada de quicadas nos 2 slots não pode
// perder bordas; UID e comandos chegam em ritmo humano.
#define Q_SENSOR_DEPTH 24
#define Q_UID_DEPTH 4
#define Q_COMMAND_DEPTH 8
#define Q_ACTUATOR_DEPTH 12
#define Q_STORAGE_DEPTH 32

// ---- Anéis persistentes (segmentos × registros por segmento) ----------------
// Um segmento é sempre sobrescrito inteiro, então o mínimo retido é (N-1)×S.
#define RING_LOG_SEGMENTS 9
#define RING_LOG_PER_SEGMENT 125   // Retém de 1000 a 1125 logs.
#define RING_EVENT_SEGMENTS 9
#define RING_EVENT_PER_SEGMENT 125 // Retém de 1000 a 1125 eventos de empréstimo.
#define RING_MINUTE_SEGMENTS 7
#define RING_MINUTE_PER_SEGMENT 240  // 1 registro/min: de 24 h a 28 h.
#define RING_HOUR_SEGMENTS 8
#define RING_HOUR_PER_SEGMENT 24   // 1 registro/h: de 7 a 8 dias.

// ---- Escrita em lote -------------------------------------------------------
#define LOG_BATCH_RECORDS 32
#define LOG_FLUSH_INTERVAL_MS 30000
#define HISTORY_FLUSH_INTERVAL_MS 600000  // Amostras são perdíveis: 10 min.

// ---- Rede ------------------------------------------------------------------
#define AP_SSID "Armario-Setup"
#define AP_PASSWORD "armario-setup"  // WPA2 exige ao menos 8 caracteres.
#define STA_CONNECT_TIMEOUT_MS 20000
#define STA_RETRY_IN_AP_MS 60000
#define WS_MAX_CLIENTS 4
#define HOSTNAME "armario"

// ---- Sessão web ------------------------------------------------------------
#define AUTH_MAX_SESSIONS 4
#define AUTH_SESSION_TTL_S 1800
#define AUTH_MAX_FAILURES 5
#define AUTH_LOCKOUT_S 60
#define AUTH_HASH_ITERATIONS 2000  // Encarece força bruta sem travar o login.

// ---- Métricas --------------------------------------------------------------
#define METRICS_PERIOD_MS 1000
#define MAX_TRACKED_TASKS 32
