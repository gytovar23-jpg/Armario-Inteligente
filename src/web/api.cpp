// API REST. Handlers rodam na task do AsyncTCP (core 0): só copiam dados e
// enfileiram comandos, nunca atuam diretamente no hardware.
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <esp_system.h>

#include "auth.h"
#include "config.h"
#include "core/shared_state.h"
#include "core/usage_store.h"
#include "drivers/display.h"
#include "drivers/nfc.h"
#include "drivers/sensors.h"
#include "json_views.h"
#include "power/power.h"
#include "server.h"
#include "storage/logger.h"
#include "storage/settings.h"
#include "storage/storage.h"
#include "storage/users_db.h"
#include "streams.h"
#include "version.h"
#include "wifi_mgr.h"

namespace {
using Handler = std::function<void(AsyncWebServerRequest*)>;
using JsonHandler = std::function<void(AsyncWebServerRequest*, JsonVariantConst)>;

constexpr size_t BACKUP_MAX_BODY = 32 * 1024;

AsyncWebServer* srv;

void sendError(AsyncWebServerRequest* req, int code, const char* message) {
  auto* response = new AsyncJsonResponse();
  response->getRoot()["error"] = message;
  response->setCode(code);
  response->setLength();
  req->send(response);
}

void sendOk(AsyncWebServerRequest* req) { req->send(200, "application/json", "{\"ok\":true}"); }

bool allowPublic(AsyncWebServerRequest* req) {
  if (settings::get().publicPages || auth::authenticated(req)) return true;
  sendError(req, 401, "login necessario");
  return false;
}

bool allowAdmin(AsyncWebServerRequest* req, bool allowPendingPassword = false) {
  if (!auth::authenticated(req)) {
    sendError(req, 401, "login necessario");
    return false;
  }
  if (auth::mustChangePassword() && !allowPendingPassword) {
    sendError(req, 403, "troque a senha padrao");
    return false;
  }
  return true;
}

void publicGet(const char* path, Handler h) {
  srv->on(path, HTTP_GET, [h](AsyncWebServerRequest* req) {
    if (allowPublic(req)) h(req);
  });
}

void adminRoute(const char* path, WebRequestMethodComposite method, Handler h) {
  srv->on(path, method, [h](AsyncWebServerRequest* req) {
    if (allowAdmin(req)) h(req);
  });
}

void adminJson(const char* path, WebRequestMethodComposite method, JsonHandler h, size_t maxBody = 4096) {
  auto* handler = new AsyncCallbackJsonWebHandler(path, [h](AsyncWebServerRequest* req, JsonVariant& json) {
    if (allowAdmin(req)) h(req, json);
  });
  handler->setMethod(method);
  handler->setMaxContentLength(maxBody);
  srv->addHandler(handler);
}

uint32_t argNumber(AsyncWebServerRequest* req, const char* name, uint32_t fallback) {
  return req->hasArg(name) ? strtoul(req->arg(name).c_str(), nullptr, 10) : fallback;
}

// ---- Eventos e empréstimos --------------------------------------------------

struct EventFilter {
  uint32_t from = 0;
  uint32_t to = 0xFFFFFFFF;
  int user = -1;
  int slot = -1;
  uint32_t cutoff = 0;
  bool withUid = false;
  bool csv = false;
};

size_t formatEvent(const EventRecord& e, const EventFilter& f, char* out, size_t cap) {
  if (e.flags & REC_TIME_SYNCED) {
    if (e.ts < f.from || e.ts > f.to || e.ts < f.cutoff) return 0;
  } else if (f.from != 0) {
    return 0;  // Sem data real, o registro não pertence a um intervalo pedido.
  }
  if (f.user >= 0 && e.userId != f.user) return 0;
  if (f.slot >= 0 && e.slot != f.slot) return 0;

  char name[56];
  if (f.csv) {
    char when[48];
    web::formatTimestamp(e.ts, e.flags, when, sizeof(when));
    web::csvEscape(e.userName, name, sizeof(name));
    return snprintf(out, cap, "%s;%s;%d;%s;%s;%lu\n", when, web::eventTypeName(e.type), e.slot == 0xFF ? 0 : e.slot + 1, name, e.uid,
                    (unsigned long)e.durationSec);
  }
  web::jsonEscape(e.userName, name, sizeof(name));
  return snprintf(out, cap, "{\"ts\":%lu,\"synced\":%s,\"type\":\"%s\",\"slot\":%d,\"user\":\"%s\",\"uid\":\"%s\",\"dur\":%lu}",
                  (unsigned long)e.ts, (e.flags & REC_TIME_SYNCED) ? "true" : "false", web::eventTypeName(e.type),
                  e.slot == 0xFF ? 0 : e.slot + 1, name, f.withUid ? e.uid : "", (unsigned long)e.durationSec);
}

void sendEvents(AsyncWebServerRequest* req, EventFilter filter, uint32_t limit) {
  web::StreamSpec spec{};
  spec.ring = &storage::events();
  spec.newestFirst = true;
  spec.limit = limit;
  if (filter.csv) {
    spec.contentType = "text/csv; charset=utf-8";
    spec.header = "data;tipo;slot;usuario;uid;duracao_s\n";
    spec.footer = "";
    spec.separator = "";
    spec.filename = "historico.csv";
  } else {
    spec.contentType = "application/json";
    spec.header = "[";
    spec.footer = "]";
    spec.separator = ",";
  }
  web::sendRingStream(req, spec, [filter](const void* rec, char* out, size_t cap) {
    return formatEvent(*static_cast<const EventRecord*>(rec), filter, out, cap);
  });
}

void routeEvents() {
  publicGet("/api/events", [](AsyncWebServerRequest* req) {
    EventFilter f;
    f.cutoff = usage::retentionCutoff();
    sendEvents(req, f, min<uint32_t>(argNumber(req, "limit", 20), 100));
  });

  adminRoute("/api/history/events", HTTP_GET, [](AsyncWebServerRequest* req) {
    EventFilter f;
    f.from = argNumber(req, "from", 0);
    f.to = argNumber(req, "to", 0xFFFFFFFF);
    f.user = req->hasArg("user") ? req->arg("user").toInt() : -1;
    f.slot = req->hasArg("slot") ? req->arg("slot").toInt() - 1 : -1;
    f.cutoff = usage::retentionCutoff();
    f.withUid = true;
    f.csv = req->arg("format") == "csv";
    sendEvents(req, f, f.csv ? 0 : min<uint32_t>(argNumber(req, "limit", 200), 500));
  });
}

// ---- Histórico para gráficos -------------------------------------------------

void routeHistory() {
  publicGet("/api/history/minutes", [](AsyncWebServerRequest* req) {
    web::StreamSpec spec{};
    spec.ring = &storage::minutes();
    spec.contentType = "application/json";
    spec.header = "[";
    spec.footer = "]";
    spec.separator = ",";
    spec.step = max<uint32_t>(argNumber(req, "step", 5), 1);
    spec.limit = 1500;
    web::sendRingStream(req, spec, [](const void* rec, char* out, size_t cap) -> size_t {
      const auto* m = static_cast<const MinuteSample*>(rec);
      return snprintf(out, cap, "[%lu,%u,%u,%u,%u,%u,%u,%d,%u,%u]", (unsigned long)m->ts, m->flags, m->unlockedPct, m->slot1Pct,
                      m->slot2Pct, m->cpu0Pct, m->cpu1Pct, m->rssi, m->heapFreeKb, m->nfcReads);
    });
  });

  publicGet("/api/history/hours", [](AsyncWebServerRequest* req) {
    web::StreamSpec spec{};
    spec.ring = &storage::hours();
    spec.contentType = "application/json";
    spec.header = "[";
    spec.footer = "]";
    spec.separator = ",";
    web::sendRingStream(req, spec, [](const void* rec, char* out, size_t cap) -> size_t {
      const auto* h = static_cast<const HourSample*>(rec);
      return snprintf(out, cap, "[%lu,%u,%u]", (unsigned long)h->ts, h->flags, h->inUseAvgX50);
    });
  });
}

// ---- Logs ---------------------------------------------------------------------

const char* const LEVEL_NAMES[] = {"ERROR", "WARN", "INFO"};

void routeLogs() {
  adminRoute("/api/logs", HTTP_GET, [](AsyncWebServerRequest* req) {
    const int level = req->hasArg("level") ? req->arg("level").toInt() : -1;
    const String format = req->arg("format");
    const bool csv = format == "csv";
    const bool download = csv || format == "json";

    web::StreamSpec spec{};
    spec.ring = &storage::logs();
    spec.newestFirst = true;
    spec.limit = download ? 0 : min<uint32_t>(argNumber(req, "limit", 100), 200);
    spec.offset = argNumber(req, "offset", 0);
    if (csv) {
      spec.contentType = "text/csv; charset=utf-8";
      spec.header = "data;nivel;origem;mensagem\n";
      spec.footer = "";
      spec.separator = "";
      spec.filename = "logs.csv";
    } else {
      spec.contentType = "application/json";
      spec.header = "[";
      spec.footer = "]";
      spec.separator = ",";
      spec.filename = download ? "logs.json" : nullptr;
    }

    web::sendRingStream(req, spec, [level, csv](const void* rec, char* out, size_t cap) -> size_t {
      const auto* r = static_cast<const LogRecord*>(rec);
      if (level >= 0 && r->level != level) return 0;
      if (r->level > 2) return 0;
      char when[48];
      char msg[168];
      web::formatTimestamp(r->ts, r->flags, when, sizeof(when));
      if (csv) {
        web::csvEscape(r->msg, msg, sizeof(msg));
        return snprintf(out, cap, "%s;%s;%s;%s\n", when, LEVEL_NAMES[r->level], r->module, msg);
      }
      web::jsonEscape(r->msg, msg, sizeof(msg));
      return snprintf(out, cap, "{\"when\":\"%s\",\"level\":\"%s\",\"module\":\"%s\",\"msg\":\"%s\"}", when, LEVEL_NAMES[r->level],
                      r->module, msg);
    });
  });

  adminRoute("/api/logs", HTTP_DELETE, [](AsyncWebServerRequest* req) {
    StorageMsg msg;
    msg.kind = StorageMsg::ClearLogs;
    storage::post(msg, pdMS_TO_TICKS(50));
    sendOk(req);
  });
}

// ---- Autenticação -------------------------------------------------------------

void routeAuth() {
  auto* login = new AsyncCallbackJsonWebHandler("/api/login", [](AsyncWebServerRequest* req, JsonVariant& json) {
    char token[auth::TOKEN_LEN + 1];
    const auth::LoginResult result = auth::login(json["user"] | "", json["password"] | "", token);
    if (result == auth::LoginResult::LockedOut) return sendError(req, 429, "muitas tentativas, aguarde");
    if (result == auth::LoginResult::BadCredentials) return sendError(req, 401, "usuario ou senha incorretos");

    auto* response = new AsyncJsonResponse();
    response->getRoot()["mustChange"] = auth::mustChangePassword();
    response->setLength();
    char cookie[96];
    // HttpOnly tira o token do alcance de scripts; SameSite=Strict barra CSRF.
    snprintf(cookie, sizeof(cookie), "session=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=%d", token, AUTH_SESSION_TTL_S);
    response->addHeader("Set-Cookie", cookie);
    req->send(response);
  });
  login->setMethod(HTTP_POST);
  srv->addHandler(login);

  srv->on("/api/logout", HTTP_POST, [](AsyncWebServerRequest* req) {
    auth::logout(req);
    sendOk(req);
  });

  srv->on("/api/session", HTTP_GET, [](AsyncWebServerRequest* req) {
    auto* response = new AsyncJsonResponse();
    JsonVariant root = response->getRoot();
    root["authenticated"] = auth::authenticated(req);
    root["mustChange"] = auth::mustChangePassword();
    response->setLength();
    req->send(response);
  });

  auto* password = new AsyncCallbackJsonWebHandler("/api/password", [](AsyncWebServerRequest* req, JsonVariant& json) {
    if (!allowAdmin(req, true)) return;
    if (!auth::changePassword(json["current"] | "", json["next"] | "")) return sendError(req, 400, "senha atual incorreta ou nova senha fraca");
    sendOk(req);
  });
  password->setMethod(HTTP_POST);
  srv->addHandler(password);
}

// ---- Usuários -----------------------------------------------------------------

void routeUsers() {
  adminRoute("/api/users", HTTP_GET, [](AsyncWebServerRequest* req) {
    auto* response = new AsyncJsonResponse(true);
    users::toJson(response->getRoot().as<JsonArray>());
    response->setLength();
    req->send(response);
  });

  adminJson("/api/users", HTTP_POST, [](AsyncWebServerRequest* req, JsonVariantConst json) {
    char err[48];
    uint16_t id;
    if (!users::upsert(json.as<JsonObjectConst>(), &id, err, sizeof(err))) return sendError(req, 400, err);
    LOG_INFO("users", "Usuario %u salvo", id);
    sendOk(req);
  });

  adminRoute("/api/users", HTTP_DELETE, [](AsyncWebServerRequest* req) {
    if (!users::remove(argNumber(req, "id", 0))) return sendError(req, 404, "usuario inexistente");
    sendOk(req);
  });

  adminRoute("/api/enroll", HTTP_GET, [](AsyncWebServerRequest* req) {
    const LockerSnapshot s = shared::read();
    auto* response = new AsyncJsonResponse();
    JsonVariant root = response->getRoot();
    root["active"] = s.state == LockerState::Enroll;
    root["captureId"] = s.enrollCaptureId;
    root["uid"] = static_cast<const char*>(s.lastUid);
    response->setLength();
    req->send(response);
  });

  adminJson("/api/enroll", HTTP_POST, [](AsyncWebServerRequest* req, JsonVariantConst json) {
    const bool start = json["action"] == "start";
    shared::sendCommand(start ? AccessCommand::EnrollStart : AccessCommand::EnrollCancel);
    sendOk(req);
  });
}

// ---- Controle, diagnóstico e sistema -------------------------------------------

void routeControl() {
  adminJson("/api/control", HTTP_POST, [](AsyncWebServerRequest* req, JsonVariantConst json) {
    const char* action = json["action"] | "";
    bool queued;
    if (strcmp(action, "unlock") == 0) {
      queued = shared::sendCommand(AccessCommand::Unlock);
    } else if (strcmp(action, "lock") == 0) {
      queued = shared::sendCommand(AccessCommand::Lock);
    } else if (strcmp(action, "silence") == 0) {
      queued = shared::sendCommand(AccessCommand::Silence);
    } else {
      return sendError(req, 400, "acao desconhecida");
    }
    if (!queued) return sendError(req, 503, "fila cheia");
    sendOk(req);
  });

  // Modo diagnóstico: exercita um componente por vez para gerar evidências.
  adminJson("/api/diag", HTTP_POST, [](AsyncWebServerRequest* req, JsonVariantConst json) {
    const char* target = json["target"] | "";
    auto* response = new AsyncJsonResponse();
    JsonVariant root = response->getRoot();
    root["target"] = target;
    root["ok"] = true;
    const LockerSnapshot s = shared::read();

    if (strcmp(target, "servo") == 0) {
      // A tranca só se move em repouso: um teste não pode abrir uma sessão real.
      if (s.state != LockerState::Idle) {
        root["ok"] = false;
        root["detail"] = "disponivel apenas em repouso";
      } else {
        shared::sendActuator(ActuatorCmd::TestServo);
        root["detail"] = "destrava e retorna em 1,5 s";
      }
    } else if (strcmp(target, "buzzer") == 0) {
      shared::sendActuator(ActuatorCmd::TestBuzzer);
    } else if (strcmp(target, "green") == 0) {
      shared::sendActuator(ActuatorCmd::TestGreen);
    } else if (strcmp(target, "red") == 0) {
      shared::sendActuator(ActuatorCmd::TestRed);
    } else if (strcmp(target, "aux") == 0) {
      root["enabled"] = AUX_ACTUATOR_ENABLED;
      shared::sendActuator(ActuatorCmd::Aux, json["on"] | false);
    } else if (strcmp(target, "nfc") == 0) {
      root["ok"] = nfc::ready();
      root["firmware"] = nfc::firmwareVersion();
      root["reads"] = shared::nfcReads.load();
    } else if (strcmp(target, "oled") == 0) {
      root["ok"] = display::ready();
    } else if (strcmp(target, "sensors") == 0) {
      root["slot1"] = s.slotOccupied[0];
      root["slot2"] = s.slotOccupied[1];
      root["slot1Edges"] = sensors::edgeCount(SensorId::Slot1);
      root["slot2Edges"] = sensors::edgeCount(SensorId::Slot2);
    } else if (strcmp(target, "flash") == 0) {
      root["ok"] = storage::mounted();
      root["logErrors"] = storage::logs().writeErrors();
      root["logs"] = storage::logs().count();
      root["logsDropped"] = logger::dropped();
    } else {
      root["ok"] = false;
      root["detail"] = "alvo desconhecido";
    }
    response->setLength();
    req->send(response);
  });

  adminRoute("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* req) {
    LOG_INFO("web", "Reinicio solicitado");
    sendOk(req);
    web::scheduleReboot(500);
  });

  adminJson("/api/power", HTTP_POST, [](AsyncWebServerRequest* req, JsonVariantConst json) {
    const char* mode = json["mode"] | "";
    const uint32_t seconds = json["seconds"] | 30;
    power::Request request;
    if (strcmp(mode, "light") == 0) {
      request = power::Request::LightSleep;
    } else if (strcmp(mode, "deep") == 0) {
      request = power::Request::DeepSleep;
    } else {
      return sendError(req, 400, "modo desconhecido");
    }
    char reason[64];
    if (!power::request(request, seconds, reason, sizeof(reason))) return sendError(req, 409, reason);
    sendOk(req);
  });
}

void routeSettings() {
  adminRoute("/api/settings", HTTP_GET, [](AsyncWebServerRequest* req) {
    auto* response = new AsyncJsonResponse();
    settingsToJson(settings::get(), response->getRoot().to<JsonObject>());
    response->setLength();
    req->send(response);
  });

  adminJson("/api/settings", HTTP_POST, [](AsyncWebServerRequest* req, JsonVariantConst json) {
    char err[64];
    if (!settings::update(json.as<JsonObjectConst>(), err, sizeof(err))) return sendError(req, 400, err);
    sendOk(req);
  });

  adminRoute("/api/wifi/scan", HTTP_POST, [](AsyncWebServerRequest* req) {
    wifimgr::startScan();
    sendOk(req);
  });

  adminRoute("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest* req) {
    auto* response = new AsyncJsonResponse();
    JsonVariant root = response->getRoot();
    root["done"] = wifimgr::scanResults(root["networks"].to<JsonArray>());
    response->setLength();
    req->send(response);
  });

  // Backup sem segredos: arquivo baixado circula fora do armário, então não leva
  // nem a senha do Wi-Fi nem o hash do admin; a restauração mantém os atuais.
  adminRoute("/api/backup", HTTP_GET, [](AsyncWebServerRequest* req) {
    auto* response = new AsyncJsonResponse();
    JsonVariant root = response->getRoot();
    root["firmware"] = FW_VERSION;
    settingsToJson(settings::get(), root["settings"].to<JsonObject>());
    users::toJson(root["users"].to<JsonArray>());
    response->setLength();
    response->addHeader("Content-Disposition", "attachment; filename=\"backup-armario.json\"");
    req->send(response);
  });

  adminJson(
      "/api/restore", HTTP_POST,
      [](AsyncWebServerRequest* req, JsonVariantConst json) {
        char err[64];
        JsonObjectConst cfg = json["settings"];
        JsonArrayConst list = json["users"];
        if (cfg.isNull() || list.isNull()) return sendError(req, 400, "arquivo de backup invalido");
        if (!users::restore(list, err, sizeof(err))) return sendError(req, 400, err);
        if (!settings::update(cfg, err, sizeof(err))) return sendError(req, 400, err);
        LOG_INFO("web", "Backup restaurado");
        sendOk(req);
      },
      BACKUP_MAX_BODY);
}
}  // namespace

namespace web {

void registerApi(AsyncWebServer& server) {
  srv = &server;

  publicGet("/api/state", [](AsyncWebServerRequest* req) {
    auto* response = new AsyncJsonResponse();
    stateToJson(response->getRoot().to<JsonObject>());
    // DIAGNOSTICO TEMPORARIO
    Serial.printf("[DIAG] /api/state %u bytes, heap %u\n", (unsigned)measureJson(response->getRoot()),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
    response->setLength();
    req->send(response);
  });

  publicGet("/api/usage", [](AsyncWebServerRequest* req) {
    auto* response = new AsyncJsonResponse();
    usageToJson(response->getRoot().to<JsonObject>());
    response->setLength();
    req->send(response);
  });

  publicGet("/api/perf", [](AsyncWebServerRequest* req) {
    auto* response = new AsyncJsonResponse();
    perfToJson(response->getRoot().to<JsonObject>());
    response->setLength();
    req->send(response);
  });

  routeEvents();
  routeHistory();
  routeLogs();
  routeAuth();
  routeUsers();
  routeControl();
  routeSettings();
}

}  // namespace web
