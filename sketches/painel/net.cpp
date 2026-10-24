#include "net.h"
#include "config.h"
#include "spotify.h"
#include "clock.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>

static const char *MDNS_NAME = "painel";
static const uint8_t PIN_MAX_ATTEMPTS = 5;
static const uint32_t PIN_LOCK_MS = 60000;
static const uint32_t WIFI_RETRY_MS = 15000;

static WebServer server(80);
static DNSServer dns;
static Config cfg;
static bool setupMode = false;
static bool mdnsStarted = false;
static String apSsid, apPass, pin;
static String authState;  // "state" do OAuth: confere que o code colado veio de um login iniciado aqui
static uint8_t pinFailures = 0;
static uint32_t pinLockedAt = 0;
static uint32_t restartAt = 0;
static bool paused = false;  // Wi-Fi desligado pelo jogo

static String randomDigits(uint8_t n) {
  String s;
  for (uint8_t i = 0; i < n; i++) s += char('0' + esp_random() % 10);
  return s;
}

static String htmlEscape(const String &in) {
  String out;
  for (char c : in) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c;
    }
  }
  return out;
}

static const char *PAGE_HEAD = R"(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Painel</title>
<style>body{font-family:sans-serif;max-width:480px;margin:auto;padding:16px}
input,select,button{width:100%;padding:10px;margin:6px 0 14px;font-size:16px;box-sizing:border-box}
button{background:#1DB954;color:#fff;border:0;border-radius:6px}</style></head><body>)";
static const char *PAGE_TAIL = "</body></html>";

static void sendPage(int code, const String &body) {
  server.send(code, "text/html; charset=utf-8", String(PAGE_HEAD) + body + PAGE_TAIL);
}

// ---------- Modo Setup: cadastro do Wi-Fi ----------

static String wifiForm() {
  String options;
  int n = WiFi.scanComplete();
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      String ssid = htmlEscape(WiFi.SSID(i));
      if (ssid.length() == 0 || options.indexOf("\"" + ssid + "\"") >= 0) continue;
      options += "<option value=\"" + ssid + "\">";
    }
  } else if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true);
  }
  return "<h2>Painel: configurar Wi-Fi</h2>"
         "<form method=post action=/wifi>"
         "Rede (escolha ou digite):<input name=ssid list=redes required autocomplete=off>"
         "<datalist id=redes>" + options + "</datalist>"
         "Senha:<input name=pass type=password>"
         "<button>Salvar e conectar</button></form>"
         "<p><a href=/>Atualizar lista de redes</a></p>";
}

static void handleWifiSave() {
  String ssid = server.arg("ssid");
  if (ssid.length() == 0 || ssid.length() > 32 || server.arg("pass").length() > 64) {
    sendPage(400, "<p>Nome ou senha inválidos.</p><p><a href=/>Voltar</a></p>");
    return;
  }
  configSaveWifi(ssid, server.arg("pass"));
  Serial.printf("[net] Wi-Fi salvo: %s, reiniciando\n", ssid.c_str());
  sendPage(200, "<p>Salvo! A placa vai reiniciar e conectar em <b>" + htmlEscape(ssid) +
                    "</b>. O endereço da página de configuração aparece na tela da placa (⚙).</p>");
  restartAt = millis() + 2000;  // dá tempo da resposta chegar ao celular
}

// ---------- Modo conectado: credenciais do Spotify, protegidas por PIN ----------

static String spotifyPage() {
  String status = cfg.spRefreshToken.length() == 0 ? "não configurado"
                  : spotifyNeedsReauth()            ? "<b>precisa reautorizar</b>"
                                                    : "configurado (autorizado em " + htmlEscape(cfg.spAuthDate) + ")";
  return "<h2>Painel: Spotify</h2><p>Status: " + status + "</p>"
         "<form method=post action=/spotify/code>"
         "PIN (na tela ⚙ da placa):<input name=pin inputmode=numeric required autocomplete=off>"
         "Client ID:<input name=client_id id=cid required autocomplete=off>"
         "Client Secret:<input name=client_secret type=password required autocomplete=off>"
         "<button type=button onclick=login()>1. Abrir login do Spotify</button>"
         "<p>Faça login e clique em <b>Concordo</b>. O navegador vai mostrar um erro "
         "(\"não foi possível conectar\"): é esperado. Copie o endereço inteiro da barra "
         "(começa com <code>" SPOTIFY_REDIRECT_URI "?code=</code>) e cole abaixo.</p>"
         "Endereço da página de erro:<input name=callback_url required autocomplete=off>"
         "<button>2. Salvar</button></form>"
         "<script>function login(){var c=document.getElementById('cid').value.trim();"
         "if(!c){alert('Preencha o Client ID');return}"
         "window.open('https://accounts.spotify.com/authorize?response_type=code&client_id='+encodeURIComponent(c)"
         "+'&scope='+encodeURIComponent('" SPOTIFY_SCOPES "')"
         "+'&redirect_uri='+encodeURIComponent('" SPOTIFY_REDIRECT_URI "')"
         "+'&state=" + authState + "','_blank')}</script>"
         "<h2>Playlist do app Músicas</h2><p>Atual: <code>" + htmlEscape(cfg.kidPlaylist) + "</code></p>"
         "<form method=post action=/playlist>"
         "PIN:<input name=pin inputmode=numeric required autocomplete=off>"
         "Link da playlist (Compartilhar → Copiar link):<input name=link required autocomplete=off>"
         "<button>Salvar playlist</button></form>"
         "<p>A playlist precisa ser da conta autorizada acima (regra do Spotify para apps pessoais).</p>";
}


static bool pinLocked() {
  if (pinFailures < PIN_MAX_ATTEMPTS) return false;
  if (millis() - pinLockedAt < PIN_LOCK_MS) return true;
  pinFailures = 0;
  return false;
}

// Confere o PIN; se falhar, já responde ao navegador/script.
static bool checkPin() {
  if (pinLocked()) {
    server.send(429, "text/plain; charset=utf-8", "Muitas tentativas. Aguarde 1 minuto.");
    return false;
  }
  if (server.arg("pin") != pin) {
    if (++pinFailures >= PIN_MAX_ATTEMPTS) pinLockedAt = millis();
    Serial.printf("[net] PIN errado (%u/%u)\n", pinFailures, PIN_MAX_ATTEMPTS);
    server.send(403, "text/plain; charset=utf-8", "PIN incorreto.");
    return false;
  }
  pinFailures = 0;
  return true;
}

static void saveSpotify(const String &id, const String &secret, const String &token, const String &date) {
  configSaveSpotify(id, secret, token, date);
  configLoad(cfg);
  spotifyReloadConfig();
  Serial.printf("[net] credenciais do Spotify salvas (autorizado em %s)\n", date.c_str());
}

// Valor de um parâmetro da query string de uma URL colada (sem decodificar: code/state são URL-safe).
static String queryParam(const String &url, const String &name) {
  int q = url.indexOf('?');
  if (q < 0) return "";
  String query = "&" + url.substring(q + 1);
  int p = query.indexOf("&" + name + "=");
  if (p < 0) return "";
  p += name.length() + 2;
  int end = query.indexOf('&', p);
  int hash = query.indexOf('#', p);
  if (hash >= 0 && (end < 0 || hash < end)) end = hash;
  return end < 0 ? query.substring(p) : query.substring(p, end);
}

// Fluxo pela página: troca o code do login por um refresh token aqui mesmo na placa.
static void handleSpotifyCode() {
  if (!checkPin()) return;
  String id = server.arg("client_id"), secret = server.arg("client_secret"), url = server.arg("callback_url");
  id.trim();
  secret.trim();
  url.trim();
  String back = "<p><a href=/>Voltar</a></p>";
  if (queryParam(url, "error").length()) {
    sendPage(400, "<p>O Spotify recusou: " + htmlEscape(queryParam(url, "error")) + "</p>" + back);
    return;
  }
  String code = queryParam(url, "code");
  if (id.length() == 0 || secret.length() == 0 || code.length() == 0) {
    sendPage(400, "<p>Faltou o Client ID, o Client Secret ou o endereço com <code>?code=</code>.</p>" + back);
    return;
  }
  if (queryParam(url, "state") != authState) {
    sendPage(400, "<p>Esse endereço não veio do login aberto por esta página. Recarregue e tente de novo.</p>" + back);
    return;
  }
  String today = clockToday();
  if (today.length() == 0) {
    sendPage(503, "<p>O relógio da placa ainda não sincronizou pela internet. Tente de novo em alguns segundos "
                  "(o code vale por poucos minutos).</p>" + back);
    return;
  }
  String refresh, error;
  if (!spotifyExchangeCode(id, secret, code, refresh, error)) {
    sendPage(502, "<p>Falha ao trocar o código: " + htmlEscape(error) +
                      "</p><p>Confira Client ID/Secret e se a Redirect URI no app é exatamente <code>" SPOTIFY_REDIRECT_URI
                      "</code>. O código só vale uma vez: refaça o passo 1.</p>" + back);
    return;
  }
  saveSpotify(id, secret, refresh, today);
  sendPage(200, "<p>Pronto! Spotify autorizado em " + today + ". Pode abrir a tela do Spotify na placa.</p>");
}

// Fluxo pelo script tools/spotify_auth.py: recebe o refresh token já pronto.
static void handleSpotifySave() {
  if (!checkPin()) return;
  String id = server.arg("client_id"), secret = server.arg("client_secret"), token = server.arg("refresh_token");
  String date = server.arg("auth_date");
  id.trim();
  secret.trim();
  token.trim();
  if (id.length() == 0 || secret.length() == 0 || token.length() == 0 || date.length() != 10) {
    server.send(400, "text/plain; charset=utf-8", "Campos obrigatórios faltando.");
    return;
  }
  saveSpotify(id, secret, token, date);
  // O script lê o código 200 e o texto "OK"
  server.send(200, "text/plain; charset=utf-8", "OK");
}

// Aceita o link (open.spotify.com/playlist/ID?si=...), a URI (spotify:playlist:ID) ou só o ID
static void handlePlaylistSave() {
  if (!checkPin()) return;
  String link = server.arg("link");
  link.trim();
  int p = link.indexOf("playlist/");
  if (p >= 0) link = link.substring(p + 9);
  else if (link.startsWith("spotify:playlist:")) link = link.substring(17);
  int end = link.indexOf('?');
  if (end >= 0) link = link.substring(0, end);
  if (link.length() < 10 || link.length() > 40) {
    sendPage(400, "<p>Não reconheci o link da playlist.</p><p><a href=/>Voltar</a></p>");
    return;
  }
  configSaveKidPlaylist(link);
  configLoad(cfg);
  Serial.printf("[net] playlist do app Musicas: %s\n", link.c_str());
  sendPage(200, "<p>Playlist salva! Abra o app Músicas na placa.</p><p><a href=/>Voltar</a></p>");
}

// ---------- Rotas (as mesmas nos dois modos; cada handler confere o modo) ----------

static void handleRoot() {
  if (setupMode) sendPage(200, wifiForm());
  else sendPage(200, spotifyPage());
}

static void handleNotFound() {
  if (setupMode) {
    // Portal cativo: qualquer endereço leva ao formulário
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Nao encontrado");
  }
}

static void startServer() {
  static bool routesAdded = false;
  if (!routesAdded) {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/wifi", HTTP_POST, [] {
      if (setupMode) handleWifiSave();
      else server.send(403, "text/plain", "Fora do modo de configuracao");
    });
    server.on("/spotify", HTTP_POST, [] {
      if (!setupMode) handleSpotifySave();
      else server.send(403, "text/plain", "Configure o Wi-Fi primeiro");
    });
    server.on("/playlist", HTTP_POST, [] {
      if (!setupMode) handlePlaylistSave();
      else server.send(403, "text/plain", "Configure o Wi-Fi primeiro");
    });
    server.on("/spotify/code", HTTP_POST, [] {
      if (!setupMode) handleSpotifyCode();
      else server.send(403, "text/plain", "Configure o Wi-Fi primeiro");
    });
    server.onNotFound(handleNotFound);
    routesAdded = true;
  }
  server.begin();
}

void netStartSetup() {
  setupMode = true;
  uint64_t mac = ESP.getEfuseMac();  // MAC gravado no chip (WiFi.macAddress() só vale com o Wi-Fi já iniciado)
  char name[16];
  snprintf(name, sizeof(name), "Painel-%02X%02X", (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
  apSsid = name;
  apPass = randomDigits(8);

  WiFi.disconnect(true, true);  // desliga e esquece a rede no driver (a nossa fica na NVS do Painel)
  WiFi.mode(WIFI_AP_STA);  // STA ligado só para listar as redes
  WiFi.softAP(apSsid.c_str(), apPass.c_str());
  WiFi.scanNetworks(true);
  dns.start(53, "*", WiFi.softAPIP());
  startServer();
  Serial.printf("[net] modo configuracao: rede %s, http://%s\n", apSsid.c_str(), WiFi.softAPIP().toString().c_str());
}

// Várias redes salvas: procura as que estão por perto (busca assíncrona, a tela não trava) e conecta na de sinal
// mais forte. Com uma só, conecta direto.
static String target;  // rede da tentativa atual
static bool scanning = false;
static void connectTo(const WifiNet &net) {
  target = net.ssid;
  WiFi.begin(net.ssid.c_str(), net.pass.c_str());
  Serial.printf("[net] conectando em %s\n", net.ssid.c_str());
}
static void connectBest() {
  if (cfg.wifiCount <= 1) {
    connectTo(cfg.wifi[0]);
    return;
  }
  WiFi.disconnect();
  WiFi.scanNetworks(true);
  scanning = true;
  Serial.printf("[net] procurando as %d redes salvas\n", cfg.wifiCount);
}
static void scanDone(int found) {
  scanning = false;
  int best = -1, bestRssi = INT_MIN;
  for (int i = 0; i < found; i++)
    for (int k = 0; k < cfg.wifiCount; k++)
      if (WiFi.SSID(i) == cfg.wifi[k].ssid && WiFi.RSSI(i) > bestRssi) {
        best = k;
        bestRssi = WiFi.RSSI(i);
      }
  WiFi.scanDelete();
  if (best >= 0) connectTo(cfg.wifi[best]);
  else if (found < 0) connectTo(cfg.wifi[0]);  // a busca falhou: tenta a última cadastrada
  else Serial.printf("[net] nenhuma rede salva por perto (%d redes vistas)\n", found);
}

void netBegin() {
  configLoad(cfg);
  pin = randomDigits(6);
  authState = randomDigits(16);
  if (cfg.wifiCount == 0) {
    netStartSetup();
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  connectBest();
  clockBegin();
  startServer();
}

void netLoop() {
  if (setupMode) dns.processNextRequest();
  server.handleClient();

  if (restartAt && (int32_t)(millis() - restartAt) >= 0) ESP.restart();

  static wl_status_t last = WL_IDLE_STATUS;
  static uint32_t lastAttempt = millis();
  wl_status_t now = WiFi.status();
  if (scanning) {
    int found = WiFi.scanComplete();
    if (found == WIFI_SCAN_RUNNING) return;
    scanDone(found);
    lastAttempt = millis();
  }
  // O auto-reconnect do core só age quando uma conexão já feita cai; se a tentativa
  // inicial falhar (ex.: roteador recusou), tenta de novo periodicamente.
  if (!setupMode && !paused && now != WL_CONNECTED && millis() - lastAttempt > WIFI_RETRY_MS) {
    lastAttempt = millis();
    Serial.println("[net] nova tentativa de conexao Wi-Fi");
    WiFi.disconnect();
    connectBest();
  }
  if (!setupMode && now != last) {
    last = now;
    if (now == WL_CONNECTED) {
      Serial.printf("[net] conectado, IP %s\n", WiFi.localIP().toString().c_str());
      if (!mdnsStarted && MDNS.begin(MDNS_NAME)) {
        MDNS.addService("http", "tcp", 80);
        mdnsStarted = true;
      }
    } else {
      Serial.printf("[net] status Wi-Fi %d\n", now);
    }
  }
}

void netPause() {
  if (setupMode || paused) return;
  paused = true;
  scanning = false;  // uma busca em andamento é descartada (o Wi-Fi vai desligar)
  if (mdnsStarted) {
    MDNS.end();
    mdnsStarted = false;
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("[net] Wi-Fi desligado");
}

void netResume() {
  if (!paused) return;
  paused = false;
  WiFi.mode(WIFI_STA);
  Serial.println("[net] Wi-Fi religado");
  connectBest();
}

NetStatus netStatus() {
  NetStatus s;
  s.pin = pin;
  s.spotifyConfigured = cfg.spRefreshToken.length() > 0;
  s.spotifyAuthDate = cfg.spAuthDate;
  s.hasSavedWifi = cfg.wifiCount > 0;
  if (setupMode) {
    s.mode = NetMode::Setup;
    s.ssid = apSsid;
    s.apPass = apPass;
    s.ip = WiFi.softAPIP().toString();
    return s;
  }
  s.ssid = target;
  if (WiFi.status() == WL_CONNECTED) {
    s.mode = NetMode::Connected;
    s.ip = WiFi.localIP().toString();
  } else {
    s.mode = NetMode::Offline;
  }
  return s;
}
