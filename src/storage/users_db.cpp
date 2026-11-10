#include "users_db.h"

#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "logger.h"

namespace {
constexpr const char* PATH = "/users.json";
constexpr const char* TMP_PATH = "/users.tmp";

User table[MAX_USERS];
size_t total = 0;
uint16_t nextId = 1;
bool dirty = false;
SemaphoreHandle_t mutex;

class Guard {
 public:
  Guard() { xSemaphoreTake(mutex, portMAX_DELAY); }
  ~Guard() { xSemaphoreGive(mutex); }
};

template <size_t N>
void setText(char (&dst)[N], const char* src) {
  strlcpy(dst, src ? src : "", N);
}

int indexOfId(uint16_t id) {
  for (size_t i = 0; i < total; i++) {
    if (table[i].id == id) return i;
  }
  return -1;
}

int indexOfUid(const char* uid) {
  if (uid[0] == '\0') return -1;
  for (size_t i = 0; i < total; i++) {
    if (strcasecmp(table[i].uid, uid) == 0) return i;
  }
  return -1;
}

void writeUser(const User& u, JsonObject o) {
  o["id"] = u.id;
  o["name"] = static_cast<const char*>(u.name);
  o["ra"] = static_cast<const char*>(u.ra);
  o["email"] = static_cast<const char*>(u.email);
  o["uid"] = static_cast<const char*>(u.uid);
  o["active"] = u.active;
  o["admin"] = u.admin;
}

// Validação comum a carga, API e restauração: nome é obrigatório e UID é hexadecimal.
bool readUser(JsonObjectConst o, User& u, char* err, size_t errLen) {
  if (o["name"].is<const char*>()) setText(u.name, o["name"]);
  if (o["ra"].is<const char*>()) setText(u.ra, o["ra"]);
  if (o["email"].is<const char*>()) setText(u.email, o["email"]);
  if (o["uid"].is<const char*>()) setText(u.uid, o["uid"]);
  if (o["active"].is<bool>()) u.active = o["active"];
  if (o["admin"].is<bool>()) u.admin = o["admin"];

  if (u.name[0] == '\0') {
    snprintf(err, errLen, "nome obrigatorio");
    return false;
  }
  const size_t len = strlen(u.uid);
  if (len % 2 != 0) {
    snprintf(err, errLen, "UID invalido");
    return false;
  }
  for (size_t i = 0; i < len; i++) {
    if (!isxdigit(static_cast<unsigned char>(u.uid[i]))) {
      snprintf(err, errLen, "UID invalido");
      return false;
    }
    u.uid[i] = toupper(static_cast<unsigned char>(u.uid[i]));
  }
  return true;
}
}  // namespace

namespace users {

bool begin() {
  mutex = xSemaphoreCreateMutex();
  File f = LittleFS.open(PATH, FILE_READ);
  if (!f) return true;  // Primeira execução: cadastro vazio é um estado válido.

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, f);
  f.close();
  if (error) {
    LOG_ERROR("users", "Cadastro corrompido: %s", error.c_str());
    return false;
  }

  char err[32];
  for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
    if (total >= MAX_USERS) break;
    User u{};
    u.id = o["id"] | 0;
    if (u.id == 0 || !readUser(o, u, err, sizeof(err))) {
      LOG_WARN("users", "Registro ignorado ao carregar");
      continue;
    }
    table[total++] = u;
    if (u.id >= nextId) nextId = u.id + 1;
  }
  return true;
}

bool findByUid(const char* uidHex, User* out) {
  Guard g;
  const int i = indexOfUid(uidHex);
  if (i < 0) return false;
  *out = table[i];
  return true;
}

bool getById(uint16_t id, User* out) {
  Guard g;
  const int i = indexOfId(id);
  if (i < 0) return false;
  *out = table[i];
  return true;
}

size_t count() {
  Guard g;
  return total;
}

bool upsert(JsonObjectConst json, uint16_t* outId, char* err, size_t errLen) {
  Guard g;
  const uint16_t id = json["id"] | 0;
  int index = -1;
  if (id != 0) {
    index = indexOfId(id);
    if (index < 0) {
      snprintf(err, errLen, "usuario inexistente");
      return false;
    }
  } else if (total >= MAX_USERS) {
    snprintf(err, errLen, "limite de usuarios atingido");
    return false;
  }

  User u{};
  if (index >= 0) {
    u = table[index];
  } else {
    u.active = true;
    u.id = nextId;
  }
  if (!readUser(json, u, err, errLen)) return false;

  const int owner = indexOfUid(u.uid);
  if (owner >= 0 && owner != index) {
    snprintf(err, errLen, "cartao ja cadastrado");
    return false;
  }

  if (index >= 0) {
    table[index] = u;
  } else {
    table[total++] = u;
    nextId++;
  }
  dirty = true;
  if (outId) *outId = u.id;
  return true;
}

bool remove(uint16_t id) {
  Guard g;
  const int i = indexOfId(id);
  if (i < 0) return false;
  table[i] = table[--total];
  dirty = true;
  return true;
}

void toJson(JsonArray out) {
  Guard g;
  for (size_t i = 0; i < total; i++) writeUser(table[i], out.add<JsonObject>());
}

bool restore(JsonArrayConst json, char* err, size_t errLen) {
  // Valida tudo em memória temporária: um arquivo ruim não pode apagar o cadastro.
  std::unique_ptr<User[]> incoming(new (std::nothrow) User[MAX_USERS]);
  if (!incoming) {
    snprintf(err, errLen, "memoria insuficiente");
    return false;
  }
  size_t n = 0;
  uint16_t maxId = 0;
  for (JsonObjectConst o : json) {
    if (n >= MAX_USERS) {
      snprintf(err, errLen, "usuarios demais");
      return false;
    }
    User u{};
    u.id = o["id"] | 0;
    if (u.id == 0) {
      snprintf(err, errLen, "id invalido");
      return false;
    }
    if (!readUser(o, u, err, errLen)) return false;
    incoming[n++] = u;
    if (u.id > maxId) maxId = u.id;
  }

  Guard g;
  memcpy(table, incoming.get(), n * sizeof(User));
  total = n;
  nextId = maxId + 1;
  dirty = true;
  return true;
}

void saveIfDirty() {
  JsonDocument doc;
  {
    Guard g;
    if (!dirty) return;
    dirty = false;
    JsonArray arr = doc.to<JsonArray>();
    for (size_t i = 0; i < total; i++) writeUser(table[i], arr.add<JsonObject>());
  }

  // Grava ao lado e renomeia: rename é atômico, então queda de energia preserva
  // o arquivo antigo inteiro em vez de deixar um JSON pela metade.
  File f = LittleFS.open(TMP_PATH, FILE_WRITE);
  if (!f) {
    LOG_ERROR("users", "Falha ao abrir arquivo temporario");
    return;
  }
  const size_t written = serializeJson(doc, f);
  f.close();
  if (written == 0 || !LittleFS.rename(TMP_PATH, PATH)) LOG_ERROR("users", "Falha ao salvar cadastro");
}

}  // namespace users
