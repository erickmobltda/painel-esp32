#include "spotify.h"
#include "config.h"

#include <WiFi.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h>
#include <LittleFS.h>
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
static volatile bool artWanted = true;  // capa grande só na tela do Spotify (o app Músicas usa miniaturas)

// App Músicas: playlist, miniaturas das capas e "tocar a partir desta música"
static const char *THUMB_DIR = "/capas";
static const char *PLAYLIST_CACHE = "/musicas.txt";
static const size_t PLAYLIST_MAX = 200;
static String playlistId;                       // (stateMutex)
static SpPlaylist playlistData;                 // (stateMutex)
static volatile bool playlistRequested = false;
static volatile uint32_t thumbsVersion = 0;     // muda a cada miniatura nova gravada
static bool thumbsComplete = false;             // todas as miniaturas da playlist já estão na flash (só a tarefa)
static String playTrackUri;                     // (stateMutex) pedido de tocar
static volatile bool playRequested = false;
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
  filter["item"]["uri"] = true;
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
  playback.trackUri = item["uri"] | "";
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
                      String &retryAfter, const String &payload = "") {
  xSemaphoreTake(httpsMutex, portMAX_DELAY);
  static const char *headerKeys[] = {"Retry-After"};
  http.collectHeaders(headerKeys, 1);
  // client.stop() do core zera o contexto TLS inteiro (memset), inclusive o vínculo com o CA bundle:
  // precisa religar antes de cada conexão nova
  if (!client.connected()) setCaBundle(client);
  http.begin(client, url);
  http.addHeader("Authorization", "Bearer " + accessToken);
  // O HTTPClient não manda Content-Length com corpo vazio, e o Spotify exige (senão 411)
  if (payload.length()) http.addHeader("Content-Type", "application/json");
  else if (strcmp(method, "GET") != 0) http.addHeader("Content-Length", "0");
  uint32_t started = millis();
  int code = http.sendRequest(method, (uint8_t *)payload.c_str(), payload.length());
  int size = http.getSize();
  // Lê a resposta inteira quando há corpo (inclusive de erro): corpo esquecido na conexão mantida aberta quebra o
  // próximo pedido. 204 e tamanho 0 não têm corpo — ler ali faria o HTTPClient esperar o servidor fechar (trava).
  body = (code > 0 && code != 204 && size != 0) ? http.getString() : "";
  uint32_t elapsed = millis() - started;
  if ((code != 200 && code != 204) || elapsed > 1500) {
    Serial.printf("[spotify] %s %s: HTTP %d, %d bytes (anunciado %d) em %u ms%s%s\n", method, url.c_str() + 30, code,
                  body.length(), size, elapsed, code >= 400 ? " | " : "", code >= 400 ? body.substring(0, 200).c_str() : "");
  }
  retryAfter = http.header("Retry-After");
  // begin() não limpa os cabeçalhos do pedido anterior, só end() (com setReuse a conexão continua aberta).
  // Sem isso eles se acumulavam: depois de um PUT com corpo, todo pedido seguinte ia com "Content-Length: 140"
  // sem corpo e o Spotify respondia 400 para sempre
  http.end();
  if (code < 0) {
    char tlsError[100] = "";
    client.lastError(tlsError, sizeof(tlsError));
    Serial.printf("[spotify] conexao falhou: %s | TLS: %s\n", http.errorToString(code).c_str(), tlsError);
    // conexão caiu: fecha tudo para a próxima tentativa abrir do zero
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

static void setNotice(const String &text) {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  playback.notice = text;
  playback.noticeAt = millis();
  xSemaphoreGive(stateMutex);
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

// Baixa um JPEG (na PSRAM) pela conexão de imagens mantida aberta. nullptr se falhar.
static uint8_t *downloadJpeg(NetworkClientSecure &client, HTTPClient &http, const String &url, int &len) {
  uint8_t *jpg = nullptr;
  xSemaphoreTake(httpsMutex, portMAX_DELAY);
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
      Serial.printf("[spotify] imagem: recebidos %d de %d bytes\n", got, len);
      free(jpg);
      jpg = nullptr;
    }
  } else {
    Serial.printf("[spotify] imagem: HTTP %d, %d bytes\n", code, len);
  }
  http.end();  // com setReuse(true) a conexão continua aberta para a próxima imagem
  if (!jpg) client.stop();
  xSemaphoreGive(httpsMutex);
  return jpg;
}

String spotifyThumbPath(const String &albumId) { return String(THUMB_DIR) + "/" + albumId + ".565"; }

// Baixa a capa de 300 px, decodifica em 150x150 e reduz para SP_THUMB_SIZE, gravando na flash (RGB565 cru).
static bool saveThumb(NetworkClientSecure &client, HTTPClient &http, const SpPlaylistItem &it) {
  if (LittleFS.totalBytes() - LittleFS.usedBytes() < 64 * 1024) {
    Serial.println("[musicas] flash cheia: sem espaco para mais capas");
    return false;
  }
  int len = 0;
  uint8_t *jpg = downloadJpeg(client, http, it.artUrl, len);
  if (!jpg) return false;
  int w = 0, h = 0;
  uint16_t *pixels = decodeJpeg(jpg, len, w, h);
  free(jpg);
  if (!pixels) return false;
  const size_t size = SP_THUMB_SIZE * SP_THUMB_SIZE * 2;
  uint16_t *thumb = (uint16_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);  // 25 KB: nunca na RAM interna
  bool ok = false;
  if (thumb) {
    for (int y = 0; y < SP_THUMB_SIZE; y++)
      for (int x = 0; x < SP_THUMB_SIZE; x++) thumb[y * SP_THUMB_SIZE + x] = pixels[(y * h / SP_THUMB_SIZE) * w + x * w / SP_THUMB_SIZE];
    if (!LittleFS.exists(THUMB_DIR)) LittleFS.mkdir(THUMB_DIR);  // a LittleFS não cria pastas ao abrir
    File f = LittleFS.open(spotifyThumbPath(it.albumId), "w");
    ok = f && f.write((const uint8_t *)thumb, size) == size;
    f.close();
    if (!ok) Serial.printf("[musicas] falha ao gravar a capa %s na flash\n", it.albumId);
    free(thumb);
  }
  free(pixels);
  if (ok) {
    thumbsVersion++;
    Serial.printf("[musicas] capa %s gravada (flash: %u de %u KB usados)\n", it.albumId,
                  LittleFS.usedBytes() / 1024, LittleFS.totalBytes() / 1024);
  }
  return ok;
}

static void savePlaylistCache(const SpPlaylist &pl) {
  File f = LittleFS.open(PLAYLIST_CACHE, "w");
  if (!f) return;
  for (const SpPlaylistItem &it : pl.items) f.printf("%s\t%s\t%s\n", it.uri, it.albumId, it.artUrl);
  f.close();
}
static void loadPlaylistCache(SpPlaylist &pl) {
  File f = LittleFS.open(PLAYLIST_CACHE, "r");
  if (!f) return;
  while (f.available() && pl.items.size() < PLAYLIST_MAX) {
    String line = f.readStringUntil('\n');
    int a = line.indexOf('\t'), b = line.indexOf('\t', a + 1);
    if (a < 0 || b < 0) continue;
    SpPlaylistItem it;
    strlcpy(it.uri, line.substring(0, a).c_str(), sizeof(it.uri));
    strlcpy(it.albumId, line.substring(a + 1, b).c_str(), sizeof(it.albumId));
    strlcpy(it.artUrl, line.substring(b + 1).c_str(), sizeof(it.artUrl));
    pl.items.push_back(it);
  }
  f.close();
  pl.loaded = !pl.items.empty();
}

// GET /playlists/{id}/items (só devolve as músicas de playlists do próprio usuário — regra de fev/2026)
static void fetchPlaylist(NetworkClientSecure &client, HTTPClient &http) {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  String id = playlistId;
  xSemaphoreGive(stateMutex);
  JsonDocument filter;
  filter["next"] = true;
  filter["items"][0]["item"]["uri"] = true;
  filter["items"][0]["item"]["album"]["id"] = true;
  filter["items"][0]["item"]["album"]["images"][0]["url"] = true;
  filter["items"][0]["item"]["album"]["images"][0]["width"] = true;

  SpPlaylist result;
  String url = "https://api.spotify.com/v1/playlists/" + id + "/items?limit=50&market=from_token";
  while (url.length() && result.items.size() < PLAYLIST_MAX) {
    String body, retryAfter;
    int code = apiRequest(client, http, "GET", url, body, retryAfter);
    if (code != 200) {
      if (code == 401) accessToken = "";
      Serial.printf("[musicas] playlist: HTTP %d\n", code);
      result.failed = true;
      break;
    }
    JsonDocument doc;
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
      result.failed = true;
      break;
    }
    for (JsonObject entry : doc["items"].as<JsonArray>()) {
      JsonObject item = entry["item"];
      if (item.isNull() || !item["uri"].is<const char *>()) continue;
      SpPlaylistItem it;
      strlcpy(it.uri, item["uri"], sizeof(it.uri));
      strlcpy(it.albumId, item["album"]["id"] | "", sizeof(it.albumId));
      int best = INT_MAX;
      for (JsonObject img : item["album"]["images"].as<JsonArray>()) {  // menor imagem com >= 150 px (a de 300)
        int w = img["width"] | 0;
        const char *url = img["url"] | "";
        if (w >= 150 && w < best && strlen(url) < sizeof(it.artUrl)) {  // URL maior que o campo: fica sem capa
          best = w;
          strlcpy(it.artUrl, url, sizeof(it.artUrl));
        }
      }
      result.items.push_back(it);
    }
    url = doc["next"] | "";
  }
  if (!result.failed) {
    result.loaded = true;
    thumbsComplete = false;
    savePlaylistCache(result);
    Serial.printf("[musicas] playlist: %u musicas\n", result.items.size());
  }
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (result.failed) {
    playlistData.failed = !playlistData.loaded;  // com cache, segue mostrando o que já tinha
  } else {
    result.version = playlistData.version + 1;
    playlistData = result;
  }
  xSemaphoreGive(stateMutex);
}

// Próxima miniatura que ainda não está na flash (álbuns repetidos dividem a mesma). false = todas prontas.
static bool nextMissingThumb(SpPlaylistItem &out) {
  if (thumbsComplete) return false;
  // Um item por vez (confere a flash fora do mutex): copiar a lista inteira gastava ~10 KB de RAM interna
  for (size_t i = 0;; i++) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    size_t count = playlistData.items.size();
    if (i < count) out = playlistData.items[i];
    xSemaphoreGive(stateMutex);
    if (i >= count) {
      thumbsComplete = count > 0;
      break;
    }
    if (out.albumId[0] && out.artUrl[0] && !LittleFS.exists(spotifyThumbPath(out.albumId))) return true;
  }
  if (thumbsComplete) Serial.println("[musicas] nada mais para baixar");
  return false;
}

// Toca a música dentro da playlist (segue a playlist depois). Sem aparelho ativo (404), escolhe um da lista:
// de preferência o celular/computador (só aparece com o app do Spotify aberto: alguém escolheu ouvir ali),
// senão uma caixa de som (a Alexa aparece como "Speaker" mesmo parada).
static void playInPlaylist(NetworkClientSecure &client, HTTPClient &http) {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  String body = "{\"context_uri\":\"spotify:playlist:" + playlistId + "\",\"offset\":{\"uri\":\"" + playTrackUri + "\"}}";
  xSemaphoreGive(stateMutex);
  String resp, retryAfter;
  int code = apiRequest(client, http, "PUT", String(API_URL) + "/play", resp, retryAfter, body);
  if (code == 404) {
    apiRequest(client, http, "GET", String(API_URL) + "/devices", resp, retryAfter);
    JsonDocument doc;
    deserializeJson(doc, resp);
    String deviceId;
    for (JsonObject d : doc["devices"].as<JsonArray>()) {
      Serial.printf("[musicas] aparelho: %s (%s)%s%s\n", d["name"] | "?", d["type"] | "?",
                    (d["is_active"] | false) ? " ativo" : "", (d["is_restricted"] | false) ? " restrito" : "");
      if (d["is_restricted"] | false) continue;
      bool speaker = strcmp(d["type"] | "", "Speaker") == 0;
      if (deviceId.isEmpty() || !speaker) deviceId = d["id"].as<String>();
      if (!speaker) break;
    }
    if (deviceId.isEmpty()) {
      setNotice("Nenhum aparelho: abra o Spotify no celular ou chame a Alexa");
      return;
    }
    code = apiRequest(client, http, "PUT", String(API_URL) + "/play?device_id=" + deviceId, resp, retryAfter, body);
  }
  if (code == 200 || code == 202 || code == 204) return;
  if (code == 401) accessToken = "";
  setNotice(code == 403 ? "O Spotify não permitiu tocar" : "Falha ao tocar (" + String(code) + ")");
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
  int len = 0;
  uint8_t *jpg = downloadJpeg(client, http, url, len);
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
      // Deixa o access token pronto (renovar leva ~1,3 s): abrir o Músicas só paga a conexão com a API.
      // Não com a tela apagada (economia) nem no jogo (Wi-Fi desligado)
      xSemaphoreTake(stateMutex, portMAX_DELAY);
      bool canRefresh = cfg.spRefreshToken.length() && !needsReauth;
      xSemaphoreGive(stateMutex);
      if (!suspended && canRefresh && WiFi.status() == WL_CONNECTED &&
          (accessToken.length() == 0 || (int32_t)(accessTokenExpiresAt - millis()) < 300000))
        refreshAccessToken();
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(60000));
      continue;
    }
    // App Músicas baixando capas: só a conexão de imagens fica aberta (duas sessões TLS não cabem na
    // RAM interna). A consulta do player pausa até terminar; pedidos de tocar e comandos têm prioridade.
    SpPlaylistItem missing;
    if (!artWanted && !playRequested && !playlistRequested && uxQueueMessagesWaiting(cmdQueue) == 0 &&
        nextMissingThumb(missing)) {
      http.end();
      client.stop();
      if (!saveThumb(artClient, artHttp, missing)) {
        // falhou: marca como sem capa nesta sessão para não ficar tentando em laço
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        for (SpPlaylistItem &it : playlistData.items)
          if (strcmp(it.albumId, missing.albumId) == 0) it.artUrl[0] = 0;
        xSemaphoreGive(stateMutex);
      }
      continue;
    }
    if (!artWanted) {  // capas prontas (ou pedido urgente): libera a conexão de imagens antes de usar a API
      artHttp.end();
      artClient.stop();
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
    if (playRequested) {
      playRequested = false;
      playInPlaylist(client, http);
      wait = 0;  // consulta logo em seguida
    }
    if (playlistRequested) {
      playlistRequested = false;
      fetchPlaylist(client, http);
    }
    if (artWanted && (artReload || (artUrl != artUrlLoaded && (int32_t)(millis() - artRetryAt) >= 0))) {
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

void spotifySetArtWanted(bool wanted) { artWanted = wanted; }

void spotifyRequestPlaylist(const String &id) {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (id != playlistId) {
    playlistId = id;
    thumbsComplete = false;
    playlistData = SpPlaylist();
    loadPlaylistCache(playlistData);  // mostra na hora o que já estava salvo
    playlistData.version = 1;
  }
  xSemaphoreGive(stateMutex);
  playlistRequested = true;
  xTaskNotifyGive(taskHandle);
}

SpPlaylist spotifyPlaylist() {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  SpPlaylist copy = playlistData;
  xSemaphoreGive(stateMutex);
  return copy;
}

uint32_t spotifyThumbsVersion() { return thumbsVersion; }

uint32_t spotifyPlaylistVersion() {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  uint32_t v = playlistData.version + (playlistData.failed ? 0x80000000u : 0);
  xSemaphoreGive(stateMutex);
  return v;
}

void spotifyPlayInPlaylist(const String &trackUri) {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  playTrackUri = trackUri;
  playback.trackUri = trackUri;  // a moldura já vai para a capa tocada
  playback.isPlaying = true;
  // Otimista: a tela já mostra "tocando"; se o Spotify recusar, a próxima consulta corrige (ex.: 204 → sem aparelho)
  if (playback.state == SpState::NoDevice || playback.state == SpState::Loading) playback.state = SpState::Ok;
  xSemaphoreGive(stateMutex);
  playRequested = true;
  xTaskNotifyGive(taskHandle);
}
