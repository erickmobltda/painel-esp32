#include "ui.h"
#include "power.h"
#include "net.h"

#include <Arduino.h>
#include <lvgl.h>

// Ponteiros válidos só enquanto a tela que os contém estiver ativa (zerados em loadScreen)
static lv_obj_t *batteryLabel = nullptr;
static lv_obj_t *wifiLabel = nullptr;
static lv_obj_t *infoLabel = nullptr;
static lv_timer_t *refreshTimer = nullptr;

static void logMemory(const char *screen) {
  Serial.printf("[mem] tela=%s heap_interno_livre=%u maior_bloco=%u psram_livre=%u\n", screen,
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

// Carrega a nova tela e apaga a anterior. A exclusão é adiada (async) porque normalmente
// estamos dentro do evento de clique de um botão que pertence à tela antiga.
// `refresh` (opcional) é chamado agora e a cada 2 s enquanto a tela estiver ativa.
static void loadScreen(lv_obj_t *scr, const char *name, lv_timer_cb_t refresh = nullptr) {
  if (refreshTimer) {
    lv_timer_del(refreshTimer);
    refreshTimer = nullptr;
  }
  lv_obj_t *old = lv_scr_act();
  lv_scr_load(scr);
  if (old) lv_obj_del_async(old);
  logMemory(name);
  if (refresh) {
    refresh(nullptr);
    refreshTimer = lv_timer_create(refresh, 2000, nullptr);
  }
}

static lv_obj_t *createScreen() {
  batteryLabel = wifiLabel = infoLabel = nullptr;
  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x101010), 0);
  lv_obj_set_style_text_color(scr, lv_color_white(), 0);
  return scr;
}

static lv_obj_t *createButton(lv_obj_t *parent, const char *text, lv_color_t color, lv_coord_t w, lv_coord_t h,
                              lv_event_cb_t cb) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, w, h);
  lv_obj_set_style_bg_color(btn, color, 0);
  lv_obj_set_style_radius(btn, 20, 0);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *label = lv_label_create(btn);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return btn;
}

static lv_obj_t *createInfoLabel(lv_obj_t *scr) {
  lv_obj_t *label = lv_label_create(scr);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
  lv_obj_set_width(label, 450);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 15, 15);
  return label;
}

static void homeEvent(lv_event_t *) {
  uiShowHome();
}

// ---------- Tela inicial ----------

static const char *batterySymbol(int percent) {
  if (percent >= 90) return LV_SYMBOL_BATTERY_FULL;
  if (percent >= 65) return LV_SYMBOL_BATTERY_3;
  if (percent >= 35) return LV_SYMBOL_BATTERY_2;
  if (percent >= 10) return LV_SYMBOL_BATTERY_1;
  return LV_SYMBOL_BATTERY_EMPTY;
}

static void refreshHome(lv_timer_t *) {
  PowerStatus p = powerRead();
  if (!p.batteryConnected) {
    lv_label_set_text(batteryLabel, LV_SYMBOL_USB " Sem bateria");
  } else if (p.charging) {
    lv_label_set_text_fmt(batteryLabel, LV_SYMBOL_CHARGE " %s %d%%", batterySymbol(p.batteryPercent), p.batteryPercent);
  } else {
    lv_label_set_text_fmt(batteryLabel, "%s %d%%%s", batterySymbol(p.batteryPercent), p.batteryPercent,
                          p.vbusIn ? " " LV_SYMBOL_USB : "");
  }

  NetStatus n = netStatus();
  switch (n.mode) {
    case NetMode::Setup:
      lv_obj_set_style_text_color(wifiLabel, lv_color_hex(0xE67E22), 0);
      lv_label_set_text(wifiLabel, LV_SYMBOL_WIFI " configurar");
      break;
    case NetMode::Connected:
      lv_obj_set_style_text_color(wifiLabel, lv_color_white(), 0);
      lv_label_set_text_fmt(wifiLabel, LV_SYMBOL_WIFI " %s", n.ssid.c_str());
      break;
    case NetMode::Offline:
      lv_obj_set_style_text_color(wifiLabel, lv_color_hex(0x808080), 0);
      lv_label_set_text(wifiLabel, LV_SYMBOL_WIFI " sem conexao");
      break;
  }
}

static void settingsEvent(lv_event_t *);

static void showPlaceholder(const char *title) {
  lv_obj_t *scr = createScreen();
  lv_obj_t *label = lv_label_create(scr);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
  lv_label_set_text_fmt(label, "%s\n\nEm breve", title);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, -50);
  lv_obj_t *btn = createButton(scr, LV_SYMBOL_LEFT " Voltar", lv_palette_main(LV_PALETTE_BLUE), 200, 70, homeEvent);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);
  loadScreen(scr, title);
}

static void spotifyEvent(lv_event_t *) {
  showPlaceholder("Spotify");
}

static void gameEvent(lv_event_t *) {
  showPlaceholder("Jogo");
}

void uiShowHome() {
  lv_obj_t *scr = createScreen();

  // Barra de status: Wi-Fi | ⚙ | bateria
  wifiLabel = lv_label_create(scr);
  lv_obj_set_width(wifiLabel, 170);
  lv_label_set_long_mode(wifiLabel, LV_LABEL_LONG_DOT);
  lv_label_set_text(wifiLabel, "");
  lv_obj_align(wifiLabel, LV_ALIGN_TOP_LEFT, 10, 12);

  lv_obj_t *gear = lv_btn_create(scr);
  lv_obj_set_size(gear, 80, 40);
  lv_obj_set_style_bg_color(gear, lv_color_hex(0x404040), 0);
  lv_obj_align(gear, LV_ALIGN_TOP_MID, 0, 4);
  lv_obj_add_event_cb(gear, settingsEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *gearLabel = lv_label_create(gear);
  lv_label_set_text(gearLabel, LV_SYMBOL_SETTINGS);
  lv_obj_center(gearLabel);

  batteryLabel = lv_label_create(scr);
  lv_label_set_text(batteryLabel, "");
  lv_obj_align(batteryLabel, LV_ALIGN_TOP_RIGHT, -10, 12);

  lv_obj_t *spotifyBtn = createButton(scr, LV_SYMBOL_AUDIO "  Spotify", lv_color_hex(0x1DB954), 210, 250, spotifyEvent);
  lv_obj_align(spotifyBtn, LV_ALIGN_BOTTOM_LEFT, 20, -20);

  lv_obj_t *gameBtn = createButton(scr, LV_SYMBOL_PLAY "  Jogo", lv_color_hex(0xE67E22), 210, 250, gameEvent);
  lv_obj_align(gameBtn, LV_ALIGN_BOTTOM_RIGHT, -20, -20);

  loadScreen(scr, "home", refreshHome);
}

// ---------- Cadastro de Wi-Fi (ponto de acesso próprio) ----------

static void refreshWifiSetup(lv_timer_t *) {
  NetStatus n = netStatus();
  lv_label_set_text_fmt(infoLabel,
                        "Configurar Wi-Fi\n\n"
                        "1. No celular, entre na rede #E67E22 %s#\n"
                        "    com a senha #E67E22 %s#\n"
                        "2. A pagina abre sozinha. Se nao abrir,\n"
                        "    acesse #E67E22 http://%s#\n"
                        "3. Escolha sua rede e digite a senha.",
                        n.ssid.c_str(), n.apPass.c_str(), n.ip.c_str());
}

static void restartEvent(lv_event_t *) {
  ESP.restart();
}

void uiShowWifiSetup() {
  lv_obj_t *scr = createScreen();
  infoLabel = createInfoLabel(scr);
  lv_obj_set_style_text_font(infoLabel, &lv_font_montserrat_20, 0);
  lv_label_set_recolor(infoLabel, true);

  NetStatus n = netStatus();
  if (n.hasSavedWifi) {
    // Troca de rede: cancelar = reiniciar e voltar para a rede salva
    lv_obj_t *cancel = createButton(scr, "Cancelar", lv_color_hex(0x606060), 200, 60, restartEvent);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 20, -15);
  }
  lv_obj_t *back = createButton(scr, LV_SYMBOL_HOME " Inicio", lv_palette_main(LV_PALETTE_BLUE), 200, 60, homeEvent);
  lv_obj_align(back, LV_ALIGN_BOTTOM_RIGHT, -20, -15);

  loadScreen(scr, "wifi_setup", refreshWifiSetup);
}

// ---------- Configurações ----------

static void refreshSettings(lv_timer_t *) {
  NetStatus n = netStatus();
  String wifi = n.mode == NetMode::Connected ? n.ssid + " (conectado)" : n.ssid + " (sem conexao, tentando...)";
  String page = n.mode == NetMode::Connected ? "http://" + n.ip + "\n     ou http://painel.local" : "-";
  String spotify = n.spotifyConfigured ? "configurado (autorizado em " + n.spotifyAuthDate + ")" : "nao configurado";
  lv_label_set_text_fmt(infoLabel,
                        "Configuracoes\n\n"
                        "Wi-Fi: %s\n"
                        "Pagina de configuracao: %s\n"
                        "PIN: #1DB954 %s#\n"
                        "Spotify: %s",
                        wifi.c_str(), page.c_str(), n.pin.c_str(), spotify.c_str());
}

static void changeWifiEvent(lv_event_t *) {
  netStartSetup();
  uiShowWifiSetup();
}

static void settingsEvent(lv_event_t *) {
  if (netStatus().mode == NetMode::Setup) {
    uiShowWifiSetup();
    return;
  }
  lv_obj_t *scr = createScreen();
  infoLabel = createInfoLabel(scr);
  lv_obj_set_style_text_font(infoLabel, &lv_font_montserrat_20, 0);
  lv_label_set_recolor(infoLabel, true);

  lv_obj_t *change = createButton(scr, "Trocar Wi-Fi", lv_color_hex(0xE67E22), 200, 60, changeWifiEvent);
  lv_obj_align(change, LV_ALIGN_BOTTOM_LEFT, 20, -15);
  lv_obj_t *back = createButton(scr, LV_SYMBOL_LEFT " Voltar", lv_palette_main(LV_PALETTE_BLUE), 200, 60, homeEvent);
  lv_obj_align(back, LV_ALIGN_BOTTOM_RIGHT, -20, -15);

  loadScreen(scr, "settings", refreshSettings);
}
