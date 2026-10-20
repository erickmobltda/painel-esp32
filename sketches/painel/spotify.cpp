#include "spotify.h"
#include "config.h"

#include <WiFi.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h>

// Lista de autoridades certificadoras embutida no core (ESP-IDF, CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL)
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

static const char *TOKEN_URL = "https://accounts.spotify.com/api/token";
static const char *PLAYER_URL = "https://api.spotify.com/v1/me/player?additional_types=track,episode&market=from_token";
static const uint32_t POLL_MS = 1000;
static const uint32_t RETRY_MS = 5000;
static const uint32_t HTTP_TIMEOUT_MS = 8000;

static TaskHandle_t taskHandle = nullptr;
static SemaphoreHandle_t stateMutex;  // protege playback, cfg e needsReauth
static SemaphoreHandle_t httpsMutex;  // uma conexão TLS nova por vez (RAM interna)
static volatile bool active = false;
static SpPlayback playback;
static Config cfg;
static bool needsReauth = false;

static String accessToken;  // só usado pela tarefa
static uint32_t accessTokenExpiresAt = 0;

static void setCaBundle(NetworkClientSecure &client) {
  client.setCACertBundle(x509_crt_bundle_start, x509_crt_bundle_end - x509_crt_bundle_start);
}

static String urlEncode(const String &in) {
  static const char *hex = "0123456789ABCDEF";
  String out;
  for (char c : in) {
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

static void setState(SpState state, const String &error = "") {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  playback.state = state;
  playback.error = error;
  xSemaphoreGive(stateMutex);
}

// POST no endpoint de token. Devolve o código HTTP (<0 = erro de conexão) e o JSON da resposta.
static int postToken(const String &clientId, const String &clientSecret, const String &body, JsonDocument &resp) {
  xSemaphoreTake(httpsMutex, portMAX_DELAY);
  int code;
  {
    NetworkClientSecure client;
    setCaBundle(client);
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.begin(client, TOKEN_URL);
    http.addHeader("Authorization", "Basic " + base64::encode(clientId + ":" + clientSecret));
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    code = http.POST(body);
    if (code > 0) {
      DeserializationError err = deserializeJson(resp, http.getString());
      if (err) Serial.printf("[spotify] token: JSON invalido (%s)\n", err.c_str());
    }
    http.end();
  }
  xSemaphoreGive(httpsMutex);
  return code;
}

static bool refreshAccessToken() {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  String id = cfg.spClientId, secret = cfg.spClientSecret, refresh = cfg.spRefreshToken;
  xSemaphoreGive(stateMutex);

  JsonDocument resp;
  int code = postToken(id, secret, "grant_type=refresh_token&refresh_token=" + urlEncode(refresh), resp);
  if (code == 200 && resp["access_token"].is<const char *>()) {
    accessToken = resp["access_token"].as<String>();
    accessTokenExpiresAt = millis() + resp["expires_in"].as<uint32_t>() * 1000;
    // Se vier um refresh token novo, ele substitui o antigo; se não vier, o atual continua valendo
    if (resp["refresh_token"].is<const char *>()) {
      String newRefresh = resp["refresh_token"].as<String>();
      if (newRefresh != refresh) {
        configSaveRefreshToken(newRefresh);
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        cfg.spRefreshToken = newRefresh;
        xSemaphoreGive(stateMutex);
        Serial.println("[spotify] refresh token novo salvo na NVS");
      }
    }
    Serial.printf("[spotify] access token renovado (expira em %us)\n", resp["expires_in"].as<unsigned>());
    return true;
  }
  if (code == 400 && resp["error"] == "invalid_grant") {
    Serial.println("[spotify] invalid_grant: precisa reautorizar");
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    needsReauth = true;
    xSemaphoreGive(stateMutex);
    setState(SpState::NeedsReauth);
    return false;
  }
  Serial.printf("[spotify] falha ao renovar token: HTTP %d %s\n", code, resp["error"] | "");
  setState(SpState::Error, code < 0 ? "Sem conexão com o Spotify" : "Spotify respondeu " + String(code));
  return false;
}

static void parsePlayer(const String &body) {
  JsonDocument filter;
  filter["is_playing"] = true;
  filter["progress_ms"] = true;
  filter["device"]["name"] = true;
  filter["item"]["name"] = true;
  filter["item"]["duration_ms"] = true;
  filter["item"]["artists"][0]["name"] = true;
  filter["item"]["show"]["name"] = true;  // podcast

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    Serial.printf("[spotify] player: JSON invalido (%s)\n", err.c_str());
    setState(SpState::Error, "Resposta inválida do Spotify");
    return;
  }
  JsonObject item = doc["item"];
  if (item.isNull()) {
    setState(SpState::NoDevice);
    return;
  }
  String artist;
  for (JsonObject a : item["artists"].as<JsonArray>()) {
    if (artist.length()) artist += ", ";
    artist += a["name"].as<const char *>();
  }
  if (artist.length() == 0) artist = item["show"]["name"] | "";

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  playback.state = SpState::Ok;
  playback.error = "";
  playback.isPlaying = doc["is_playing"];
  playback.title = item["name"] | "";
  playback.artist = artist;
  playback.device = doc["device"]["name"] | "";
  playback.progressMs = doc["progress_ms"];
  playback.durationMs = item["duration_ms"];
  playback.fetchedAt = millis();
  xSemaphoreGive(stateMutex);
}

// Uma consulta ao player. Devolve quanto esperar até a próxima.
static uint32_t pollOnce(NetworkClientSecure &client, HTTPClient &http) {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  bool configured = cfg.spRefreshToken.length() && cfg.spClientId.length() && cfg.spClientSecret.length();
  bool reauth = needsReauth;
  xSemaphoreGive(stateMutex);

  if (!configured) {
    setState(SpState::NotConfigured);
    return RETRY_MS;
  }
  if (reauth) {
    setState(SpState::NeedsReauth);  // não fica tentando: espera credenciais novas
    return RETRY_MS;
  }
  if (WiFi.status() != WL_CONNECTED) {
    setState(SpState::NoWifi);
    return RETRY_MS;
  }
  if (accessToken.length() == 0 || (int32_t)(accessTokenExpiresAt - millis()) < 60000) {
    if (!refreshAccessToken()) return RETRY_MS;
  }

  xSemaphoreTake(httpsMutex, portMAX_DELAY);
  static const char *headerKeys[] = {"Retry-After"};
  http.collectHeaders(headerKeys, 1);
  // client.stop() do core zera o contexto TLS inteiro (memset), inclusive o vínculo com o CA bundle:
  // precisa religar antes de cada conexão nova
  if (!client.connected()) setCaBundle(client);
  http.begin(client, PLAYER_URL);
  http.addHeader("Authorization", "Bearer " + accessToken);
  uint32_t started = millis();
  int code = http.GET();
  int size = http.getSize();
  String body = code == 200 ? http.getString() : "";
  uint32_t elapsed = millis() - started;
  if ((code != 200 && code != 204) || elapsed > 1500) {
    Serial.printf("[spotify] GET player: HTTP %d, %d bytes (tamanho anunciado %d) em %u ms\n", code, body.length(), size,
                  elapsed);
  }
  String retryAfter = http.header("Retry-After");
  if (code < 0) {
    char tlsError[100] = "";
    client.lastError(tlsError, sizeof(tlsError));
    Serial.printf("[spotify] conexao falhou: %s | TLS: %s\n", http.errorToString(code).c_str(), tlsError);
    // conexão caiu: fecha tudo para a próxima tentativa abrir do zero
    http.end();
    client.stop();
  }
  xSemaphoreGive(httpsMutex);

  switch (code) {
    case 200:
      parsePlayer(body);
      return POLL_MS;
    case 204:
      setState(SpState::NoDevice);
      return POLL_MS;
    case 401:
      accessToken = "";  // expirou antes do previsto: renova na próxima
      return 0;
    case 429: {
      uint32_t secs = retryAfter.toInt() > 0 ? retryAfter.toInt() : 5;
      Serial.printf("[spotify] 429: esperando %us\n", secs);
      return secs * 1000;
    }
    default:
      Serial.printf("[spotify] player: HTTP %d\n", code);
      setState(SpState::Error, code < 0 ? "Sem conexão com o Spotify" : "Spotify respondeu " + String(code));
      return RETRY_MS;
  }
}

static void spotifyTask(void *) {
  NetworkClientSecure client;
  HTTPClient http;
  http.setReuse(true);  // mantém a conexão TLS aberta entre consultas
  http.setTimeout(HTTP_TIMEOUT_MS);

  for (;;) {
    if (!active) {
      // Fora da tela do Spotify: fecha a conexão (libera a RAM do TLS) e dorme até ser acordada
      http.end();
      client.stop();
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }
    uint32_t wait = pollOnce(client, http);
    static UBaseType_t minFree = UINT32_MAX;
    UBaseType_t free = uxTaskGetStackHighWaterMark(nullptr);
    if (free < minFree) {
      minFree = free;
      Serial.printf("[spotify] pilha livre minima: %u bytes\n", free);
    }
    if (wait) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait));
  }
}

void spotifyBegin() {
  stateMutex = xSemaphoreCreateMutex();
  httpsMutex = xSemaphoreCreateMutex();
  configLoad(cfg);
  // Núcleo 1, prioridade 0: o HTTPClient do core espera dados girando (delay(0)) sem ceder a CPU para
  // prioridades menores. No núcleo 0 isso deixava a IDLE0 sem rodar e o watchdog reiniciava a placa.
  // No núcleo 1 o watchdog não vigia a IDLE, e o loop() (tela, prioridade 1) sempre tem preferência.
  xTaskCreatePinnedToCore(spotifyTask, "spotify", 8192, nullptr, tskIDLE_PRIORITY, &taskHandle, 1);
}

void spotifySetActive(bool on) {
  active = on;
  if (on) setState(SpState::Loading);
  xTaskNotifyGive(taskHandle);
}

SpPlayback spotifyPlayback() {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  SpPlayback copy = playback;
  xSemaphoreGive(stateMutex);
  return copy;
}

void spotifyReloadConfig() {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  configLoad(cfg);
  needsReauth = false;
  xSemaphoreGive(stateMutex);
  accessTokenExpiresAt = millis();  // força renovar com as credenciais novas
  if (taskHandle) xTaskNotifyGive(taskHandle);
}

bool spotifyNeedsReauth() {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  bool r = needsReauth;
  xSemaphoreGive(stateMutex);
  return r;
}

bool spotifyExchangeCode(const String &clientId, const String &clientSecret, const String &code,
                         String &refreshTokenOut, String &errorOut) {
  JsonDocument resp;
  int status = postToken(clientId, clientSecret,
                         "grant_type=authorization_code&code=" + urlEncode(code) +
                             "&redirect_uri=" + urlEncode(SPOTIFY_REDIRECT_URI),
                         resp);
  if (status == 200 && resp["refresh_token"].is<const char *>()) {
    refreshTokenOut = resp["refresh_token"].as<String>();
    return true;
  }
  errorOut = status < 0 ? String("sem conexão com o Spotify")
                        : "HTTP " + String(status) + " " + (resp["error"] | "") + " " + (resp["error_description"] | "");
  Serial.printf("[spotify] troca do code falhou: %s\n", errorOut.c_str());
  return false;
}
