#include "net.h"
#include "config.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>

static const char *MDNS_NAME = "painel";
static const uint8_t PIN_MAX_ATTEMPTS = 5;
static const uint32_t PIN_LOCK_MS = 60000;

static WebServer server(80);
static DNSServer dns;
static Config cfg;
static bool setupMode = false;
static bool mdnsStarted = false;
static String apSsid, apPass, pin;
static uint8_t pinFailures = 0;
static uint32_t pinLockedAt = 0;
static uint32_t restartAt = 0;

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

static String spotifyForm(const String &msg) {
  String status = cfg.spRefreshToken.length() ? "configurado (autorizado em " + htmlEscape(cfg.spAuthDate) + ")"
                                              : "não configurado";
  return "<h2>Painel: Spotify</h2><p>Status: " + status + "</p>" + msg +
         "<form method=post action=/spotify>"
         "PIN (na tela ⚙ da placa):<input name=pin inputmode=numeric required autocomplete=off>"
         "Client ID:<input name=client_id required autocomplete=off>"
         "Client Secret:<input name=client_secret type=password required autocomplete=off>"
         "Refresh token:<input name=refresh_token type=password required autocomplete=off>"
         "Data da autorização:<input name=auth_date type=date id=d required>"
         "<button>Salvar</button></form>"
         "<script>document.getElementById('d').valueAsDate=new Date()</script>";
}

static bool pinLocked() {
  if (pinFailures < PIN_MAX_ATTEMPTS) return false;
  if (millis() - pinLockedAt < PIN_LOCK_MS) return true;
  pinFailures = 0;
  return false;
}

static void handleSpotifySave() {
  if (pinLocked()) {
    server.send(429, "text/plain; charset=utf-8", "Muitas tentativas. Aguarde 1 minuto.");
    return;
  }
  if (server.arg("pin") != pin) {
    if (++pinFailures >= PIN_MAX_ATTEMPTS) pinLockedAt = millis();
    Serial.printf("[net] PIN errado (%u/%u)\n", pinFailures, PIN_MAX_ATTEMPTS);
    server.send(403, "text/plain; charset=utf-8", "PIN incorreto.");
    return;
  }
  pinFailures = 0;
  String id = server.arg("client_id"), secret = server.arg("client_secret"), token = server.arg("refresh_token");
  String date = server.arg("auth_date");
  id.trim();
  secret.trim();
  token.trim();
  if (id.length() == 0 || secret.length() == 0 || token.length() == 0 || date.length() != 10) {
    server.send(400, "text/plain; charset=utf-8", "Campos obrigatórios faltando.");
    return;
  }
  configSaveSpotify(id, secret, token, date);
  configLoad(cfg);
  Serial.printf("[net] credenciais do Spotify salvas (autorizado em %s)\n", date.c_str());
  // O script tools/spotify_auth.py lê o código 200 e o texto "OK"
  server.send(200, "text/plain; charset=utf-8", "OK");
}

// ---------- Rotas (as mesmas nos dois modos; cada handler confere o modo) ----------

static void handleRoot() {
  if (setupMode) sendPage(200, wifiForm());
  else sendPage(200, spotifyForm(""));
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

void netBegin() {
  configLoad(cfg);
  pin = randomDigits(6);
  if (cfg.wifiSsid.length() == 0) {
    netStartSetup();
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
  startServer();
  Serial.printf("[net] conectando em %s\n", cfg.wifiSsid.c_str());
}

void netLoop() {
  if (setupMode) dns.processNextRequest();
  server.handleClient();

  if (restartAt && (int32_t)(millis() - restartAt) >= 0) ESP.restart();

  static wl_status_t last = WL_IDLE_STATUS;
  wl_status_t now = WiFi.status();
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

NetStatus netStatus() {
  NetStatus s;
  s.pin = pin;
  s.spotifyConfigured = cfg.spRefreshToken.length() > 0;
  s.spotifyAuthDate = cfg.spAuthDate;
  s.hasSavedWifi = cfg.wifiSsid.length() > 0;
  if (setupMode) {
    s.mode = NetMode::Setup;
    s.ssid = apSsid;
    s.apPass = apPass;
    s.ip = WiFi.softAPIP().toString();
    return s;
  }
  s.ssid = cfg.wifiSsid;
  if (WiFi.status() == WL_CONNECTED) {
    s.mode = NetMode::Connected;
    s.ip = WiFi.localIP().toString();
  } else {
    s.mode = NetMode::Offline;
  }
  return s;
}
