#include "spotify.h"
#include "config.h"

#include <WiFi.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h>
#include "extra/libs/sjpg/tjpgd.h"  // decodificador JPEG que vem com o LVGL (LV_USE_SJPG 1)

// Lista de autoridades certificadoras embutida no core (ESP-IDF, CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL)
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

static const char *TOKEN_URL = "https://accounts.spotify.com/api/token";
static const char *API_URL = "https://api.spotify.com/v1/me/player";
static const char *QUEUE_URL = "https://api.spotify.com/v1/me/player/queue";
static const size_t QUEUE_MAX_ITEMS = 20;
static const char *PLAYER_URL = "https://api.spotify.com/v1/me/player?additional_types=track,episode&market=from_token";
static const int ART_SIZE = 150;            // lado da capa na tela (px)
static const int ART_MAX_BYTES = 300000;    // JPEG baixado (as capas de 300 px têm ~20-40 KB)
static const uint32_t POLL_MS = 1000;
static const uint32_t RETRY_MS = 5000;
static const uint32_t HTTP_TIMEOUT_MS = 8000;

static TaskHandle_t taskHandle = nullptr;
static SemaphoreHandle_t stateMutex;  // protege playback, cfg e needsReauth
static SemaphoreHandle_t httpsMutex;  // uma conexão TLS nova por vez (RAM interna)
static volatile bool active = false;
static volatile bool suspended = false;  // tela apagada
static SpPlayback playback;
static Config cfg;
static bool needsReauth = false;

struct CmdMsg {
  SpCmd cmd;
  uint32_t positionMs;
};
static QueueHandle_t cmdQueue;

// Capa: URL pedida pela última consulta (stateMutex) e a já carregada (só a tarefa usa)
static String artUrlWanted, artUrlLoaded;
static volatile bool artReload = false;  // tela do Spotify reaberta: buscar a capa de novo
static uint32_t artRetryAt = 0;          // download falhou: nova tentativa a partir daqui (só a tarefa usa)
static uint16_t *pendingArt = nullptr;     // capa aguardando a tela buscar (stateMutex); nullptr = sem capa
static int pendingArtW = 0, pendingArtH = 0;
static uint32_t artVersion = 0, artVersionTaken = 0;  // (stateMutex)

static volatile bool queueRequested = false;
static SpQueue queue;  // (stateMutex)

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
  filter["item"]["album"]["images"][0]["url"] = true;
  filter["item"]["album"]["images"][0]["width"] = true;
  filter["item"]["images"][0]["url"] = true;  // episódio de podcast
  filter["item"]["images"][0]["width"] = true;

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

  // Menor imagem com pelo menos ART_SIZE px (o Spotify manda 640, 300 e 64)
  JsonArray images = item["album"]["images"].isNull() ? item["images"].as<JsonArray>() : item["album"]["images"].as<JsonArray>();
  String artUrl;
  int artWidth = INT_MAX;
  for (JsonObject img : images) {
    int w = img["width"] | 0;
    if (w >= ART_SIZE && w < artWidth) {
      artWidth = w;
      artUrl = img["url"].as<String>();
    }
  }

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
  artUrlWanted = artUrl;
  xSemaphoreGive(stateMutex);
}

// Requisição na conexão mantida aberta com api.spotify.com. Devolve o código HTTP (<0 = falha de conexão).
static int apiRequest(NetworkClientSecure &client, HTTPClient &http, const char *method, const String &url, String &body,
                      String &retryAfter) {
  xSemaphoreTake(httpsMutex, portMAX_DELAY);
  static const char *headerKeys[] = {"Retry-After"};
  http.collectHeaders(headerKeys, 1);
  // client.stop() do core zera o contexto TLS inteiro (memset), inclusive o vínculo com o CA bundle:
  // precisa religar antes de cada conexão nova
  if (!client.connected()) setCaBundle(client);
  http.begin(client, url);
  http.addHeader("Authorization", "Bearer " + accessToken);
  // O HTTPClient não manda Content-Length com corpo vazio, e o Spotify exige (senão 411)
  if (strcmp(method, "GET") != 0) http.addHeader("Content-Length", "0");
  uint32_t started = millis();
  int code = http.sendRequest(method, (uint8_t *)nullptr, 0);
  int size = http.getSize();
  body = code == 200 ? http.getString() : "";
  uint32_t elapsed = millis() - started;
  if ((code != 200 && code != 204) || elapsed > 1500) {
    Serial.printf("[spotify] %s %s: HTTP %d, %d bytes (anunciado %d) em %u ms\n", method, url.c_str() + 30, code,
                  body.length(), size, elapsed);
  }
  retryAfter = http.header("Retry-After");
  if (code < 0) {
    char tlsError[100] = "";
    client.lastError(tlsError, sizeof(tlsError));
    Serial.printf("[spotify] conexao falhou: %s | TLS: %s\n", http.errorToString(code).c_str(), tlsError);
    // conexão caiu: fecha tudo para a próxima tentativa abrir do zero
    http.end();
    client.stop();
  }
  xSemaphoreGive(httpsMutex);
  return code;
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

  String body, retryAfter;
  int code = apiRequest(client, http, "GET", PLAYER_URL, body, retryAfter);

  switch (code) {
    case 200:
      parsePlayer(body);
      return POLL_MS;
    case 204:
      setState(SpState::NoDevice);
      xSemaphoreTake(stateMutex, portMAX_DELAY);
      artUrlWanted = "";
      xSemaphoreGive(stateMutex);
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

// GET /v1/me/player/queue: a resposta traz os objetos completos das músicas (~5 KB cada, vai para a PSRAM);
// o filtro do ArduinoJson guarda só nome e artistas.
static void fetchQueue(NetworkClientSecure &client, HTTPClient &http) {
  String body, retryAfter;
  int code = apiRequest(client, http, "GET", QUEUE_URL, body, retryAfter);
  SpQueue result;
  result.loaded = true;
  if (code == 200) {
    JsonDocument filter;
    filter["queue"][0]["name"] = true;
    filter["queue"][0]["artists"][0]["name"] = true;
    filter["queue"][0]["show"]["name"] = true;  // podcast
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (err) {
      Serial.printf("[spotify] fila: JSON invalido (%s)\n", err.c_str());
      result.failed = true;
    }
    for (JsonObject item : doc["queue"].as<JsonArray>()) {
      if (result.items.size() >= QUEUE_MAX_ITEMS) break;
      SpQueueItem q;
      q.title = item["name"] | "";
      for (JsonObject a : item["artists"].as<JsonArray>()) {
        if (q.artist.length()) q.artist += ", ";
        q.artist += a["name"].as<const char *>();
      }
      if (q.artist.length() == 0) q.artist = item["show"]["name"] | "";
      result.items.push_back(q);
    }
    Serial.printf("[spotify] fila: %u itens (%u bytes)\n", result.items.size(), body.length());
  } else if (code != 204) {  // 204 = nada tocando: fila vazia
    if (code == 401) accessToken = "";
    result.failed = true;
  }
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  result.version = queue.version + 1;
  queue = result;
  xSemaphoreGive(stateMutex);
}

static void setNotice(const String &text) {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  playback.notice = text;
  playback.noticeAt = millis();
  xSemaphoreGive(stateMutex);
}

static void runCommand(NetworkClientSecure &client, HTTPClient &http, const CmdMsg &msg) {
  const char *method;
  String url = API_URL;
  switch (msg.cmd) {
    case SpCmd::Play: method = "PUT"; url += "/play"; break;
    case SpCmd::Pause: method = "PUT"; url += "/pause"; break;
    case SpCmd::Next: method = "POST"; url += "/next"; break;
    case SpCmd::Previous: method = "POST"; url += "/previous"; break;
    case SpCmd::Seek: method = "PUT"; url += "/seek?position_ms=" + String(msg.positionMs); break;
  }
  String body, retryAfter;
  int code = apiRequest(client, http, method, url, body, retryAfter);
  if (code == 200 || code == 202 || code == 204) return;
  if (code == 401) accessToken = "";
  Serial.printf("[spotify] comando recusado: HTTP %d\n", code);
  setNotice(code == 403   ? "O Spotify não permitiu esse comando"
            : code == 404 ? "Nenhum dispositivo ativo"
            : code == 429 ? "Muitos comandos, aguarde"
                          : "Falha no comando (" + String(code) + ")");
}

// ---------- Capa do álbum ----------

struct JpegCtx {
  const uint8_t *data;
  size_t len, pos;
  uint16_t *out;
  int outW, outH;
};

static size_t jpegInput(JDEC *jd, uint8_t *buf, size_t n) {
  JpegCtx *ctx = (JpegCtx *)jd->device;
  n = min(n, ctx->len - ctx->pos);
  if (buf) memcpy(buf, ctx->data + ctx->pos, n);
  ctx->pos += n;
  return n;
}

// TJpgDec entrega blocos em RGB888 (JD_FORMAT 0); converte para RGB565
static int jpegOutput(JDEC *jd, void *bitmap, JRECT *rect) {
  JpegCtx *ctx = (JpegCtx *)jd->device;
  const uint8_t *rgb = (const uint8_t *)bitmap;
  for (int y = rect->top; y <= rect->bottom; y++) {
    for (int x = rect->left; x <= rect->right; x++, rgb += 3) {
      if (x < ctx->outW && y < ctx->outH) {
        ctx->out[y * ctx->outW + x] = ((rgb[0] & 0xF8) << 8) | ((rgb[1] & 0xFC) << 3) | (rgb[2] >> 3);
      }
    }
  }
  return 1;
}

// Decodifica já reduzindo (1/2, 1/4, 1/8) para chegar perto de ART_SIZE. Devolve buffer na PSRAM ou nullptr.
static uint16_t *decodeJpeg(const uint8_t *data, size_t len, int &w, int &h) {
  void *work = malloc(4096);  // área de trabalho do TJpgDec (~3,1 KB com JD_FASTDECODE 0)
  JpegCtx ctx = {data, len, 0, nullptr, 0, 0};
  JDEC jd;
  uint16_t *out = nullptr;
  JRESULT res = work ? jd_prepare(&jd, jpegInput, work, 4096, &ctx) : JDR_MEM1;
  if (res == JDR_OK) {
    uint8_t scale = 0;
    while (scale < 3 && (jd.width >> (scale + 1)) >= ART_SIZE) scale++;
    ctx.outW = (jd.width + (1 << scale) - 1) >> scale;
    ctx.outH = (jd.height + (1 << scale) - 1) >> scale;
    ctx.out = (uint16_t *)heap_caps_malloc(ctx.outW * ctx.outH * 2, MALLOC_CAP_SPIRAM);
    if (ctx.out) res = jd_decomp(&jd, jpegOutput, scale);
    if (ctx.out && res == JDR_OK) {
      out = ctx.out;
      w = ctx.outW;
      h = ctx.outH;
    } else {
      free(ctx.out);
    }
  }
  if (res != JDR_OK) Serial.printf("[spotify] capa: falha ao decodificar JPEG (%d)\n", res);
  free(work);
  return out;
}

static void publishArt(uint16_t *pixels, int w, int h) {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  free(pendingArt);  // capa anterior que a tela não chegou a buscar
  pendingArt = pixels;
  pendingArtW = w;
  pendingArtH = h;
  artVersion++;
  xSemaphoreGive(stateMutex);
}

// Baixa e decodifica a capa. Usa uma segunda conexão mantida aberta com o servidor de imagens
// (i.scdn.co): só a primeira capa paga a abertura do TLS (~3-5 s); as seguintes levam ~1 s.
static void fetchArt(NetworkClientSecure &client, HTTPClient &http, const String &url) {
  artUrlLoaded = url;
  if (url.length() == 0) {
    publishArt(nullptr, 0, 0);
    return;
  }

  uint32_t started = millis();
  uint8_t *jpg = nullptr;
  int len = 0;
  xSemaphoreTake(httpsMutex, portMAX_DELAY);
  {
    if (!client.connected()) setCaBundle(client);  // ver apiRequest: stop() apaga o vínculo com o CA bundle
    http.begin(client, url);
    int code = http.GET();
    len = http.getSize();
    if (code == 200 && len > 0 && len <= ART_MAX_BYTES) {
      jpg = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
      NetworkClient *stream = http.getStreamPtr();
      int got = 0;
      uint32_t deadline = millis() + 10000;
      // read() devolve -1 quando ainda não chegou nada (não espera): aguarda os dados até completar
      while (jpg && got < len && (int32_t)(deadline - millis()) > 0) {
        int avail = stream->available();
        if (avail > 0) {
          int n = stream->read(jpg + got, min(avail, len - got));
          if (n > 0) got += n;
        } else if (!stream->connected()) {
          break;
        } else {
          delay(5);
        }
      }
      if (got != len) {
        Serial.printf("[spotify] capa: recebidos %d de %d bytes\n", got, len);
        free(jpg);
        jpg = nullptr;
      }
    } else {
      Serial.printf("[spotify] capa: HTTP %d, %d bytes\n", code, len);
    }
    http.end();  // com setReuse(true) a conexão continua aberta para a próxima capa
    if (!jpg) client.stop();
  }
  xSemaphoreGive(httpsMutex);
  if (!jpg) {
    publishArt(nullptr, 0, 0);  // a tela mostra o ícone no lugar
    artUrlLoaded = "";          // tenta de novo daqui a pouco
    artRetryAt = millis() + 10000;
    return;
  }

  uint32_t downloaded = millis();
  int w = 0, h = 0;
  uint16_t *pixels = decodeJpeg(jpg, len, w, h);
  free(jpg);
  Serial.printf("[spotify] capa %dx%d (%d bytes): download %u ms, decodificacao %u ms | heap interno %u, maior bloco %u\n",
                w, h, len, downloaded - started, millis() - downloaded, heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  publishArt(pixels, w, h);
}

static void spotifyTask(void *) {
  NetworkClientSecure client;
  HTTPClient http;
  http.setReuse(true);  // mantém a conexão TLS aberta entre consultas
  http.setTimeout(HTTP_TIMEOUT_MS);
  NetworkClientSecure artClient;  // segunda conexão, com o servidor das capas
  HTTPClient artHttp;
  artHttp.setReuse(true);
  artHttp.setTimeout(HTTP_TIMEOUT_MS);

  for (;;) {
    if (!active || suspended) {
      // Fora da tela do Spotify: fecha as conexões (libera a RAM do TLS) e dorme até ser acordada
      http.end();
      client.stop();
      artHttp.end();
      artClient.stop();
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }
    // Comandos primeiro; depois deles, consulta na hora para a tela refletir o resultado
    CmdMsg msg;
    while (xQueueReceive(cmdQueue, &msg, 0) == pdTRUE) {
      if (accessToken.length() == 0 || (int32_t)(accessTokenExpiresAt - millis()) < 60000) refreshAccessToken();
      runCommand(client, http, msg);
    }
    uint32_t wait = pollOnce(client, http);

    xSemaphoreTake(stateMutex, portMAX_DELAY);
    String artUrl = artUrlWanted;
    xSemaphoreGive(stateMutex);
    if (artReload || (artUrl != artUrlLoaded && (int32_t)(millis() - artRetryAt) >= 0)) {
      artReload = false;
      fetchArt(artClient, artHttp, artUrl);
    }
    if (queueRequested) {
      queueRequested = false;
      fetchQueue(client, http);
    }

    static UBaseType_t minFree = UINT32_MAX;
    UBaseType_t free = uxTaskGetStackHighWaterMark(nullptr);
    if (free < minFree) {
      minFree = free;
      Serial.printf("[spotify] pilha livre minima: %u bytes\n", free);
    }
    if (wait && uxQueueMessagesWaiting(cmdQueue) == 0) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait));
  }
}

void spotifyBegin() {
  stateMutex = xSemaphoreCreateMutex();
  httpsMutex = xSemaphoreCreateMutex();
  cmdQueue = xQueueCreate(4, sizeof(CmdMsg));
  configLoad(cfg);
  // Núcleo 1, prioridade 1 (igual ao loop()/tela, que se revezam a cada tick): o HTTPClient do core espera
  // dados girando (delay(0)) sem ceder a CPU para prioridades menores. No núcleo 0 isso deixava a IDLE0 sem
  // rodar e o watchdog reiniciava a placa; no núcleo 1 o watchdog não vigia a IDLE. Com prioridade 0 a
  // tarefa só rodava com a tela ociosa e as consultas levavam 2-9 s.
  xTaskCreatePinnedToCore(spotifyTask, "spotify", 12288, nullptr, 0, &taskHandle, 1);
}

void spotifySetActive(bool on) {
  bool wasActive = active;
  active = on;
  // Economia de energia do Wi-Fi (modem sleep) atrasa cada ida-e-volta até o próximo beacon do roteador:
  // desligada só enquanto a tela do Spotify está aberta
  WiFi.setSleep(!on);
  if (on) {
    // Vindo da fila a consulta já estava rodando: não volta para "Carregando..."
    if (!wasActive) setState(SpState::Loading);
    artReload = true;  // a tela é nova: busca a capa de novo mesmo que a música seja a mesma
  }
  xTaskNotifyGive(taskHandle);
}

void spotifyCommand(SpCmd cmd, uint32_t positionMs) {
  // Atualiza o estado local na hora para a tela responder ao toque sem esperar a rede
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  uint32_t now = millis();
  if (playback.isPlaying) playback.progressMs += now - playback.fetchedAt;
  playback.fetchedAt = now;
  if (cmd == SpCmd::Play) playback.isPlaying = true;
  if (cmd == SpCmd::Pause) playback.isPlaying = false;
  if (cmd == SpCmd::Seek) playback.progressMs = positionMs;
  xSemaphoreGive(stateMutex);

  CmdMsg msg = {cmd, positionMs};
  if (xQueueSend(cmdQueue, &msg, 0) != pdTRUE) Serial.println("[spotify] fila de comandos cheia");
  xTaskNotifyGive(taskHandle);
}

void spotifySetSuspended(bool s) {
  suspended = s;
  if (!s && taskHandle) xTaskNotifyGive(taskHandle);
}

bool spotifyTakeArt(uint16_t *&pixels, int &w, int &h) {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  bool changed = artVersion != artVersionTaken;
  if (changed) {
    artVersionTaken = artVersion;
    pixels = pendingArt;
    w = pendingArtW;
    h = pendingArtH;
    pendingArt = nullptr;
  }
  xSemaphoreGive(stateMutex);
  return changed;
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

void spotifyRequestQueue(bool clear) {
  if (clear) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    queue.loaded = false;
    queue.items.clear();
    queue.version++;
    xSemaphoreGive(stateMutex);
  }
  queueRequested = true;
  xTaskNotifyGive(taskHandle);
}

SpQueue spotifyQueue() {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  SpQueue copy = queue;
  xSemaphoreGive(stateMutex);
  return copy;
}
