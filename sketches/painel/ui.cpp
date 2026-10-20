#include "ui.h"
#include "power.h"
#include "net.h"
#include "spotify.h"
#include "clock.h"
#include "fonts.h"
#include "game.h"
#include "sound.h"

#include <Arduino.h>
#include <lvgl.h>

// Ponteiros válidos só enquanto a tela que os contém estiver ativa (zerados em loadScreen)
static lv_obj_t *batteryLabel = nullptr;
static lv_obj_t *wifiLabel = nullptr;
static lv_obj_t *infoLabel = nullptr;
static lv_obj_t *spContent, *spTitle, *spArtist, *spDevice, *spElapsed, *spTotal, *spMessage, *spSlider, *spPlayLabel,
    *spArt, *spNotice;
static lv_timer_t *refreshTimer = nullptr;
static void (*onLeave)() = nullptr;  // chamado quando a tela atual é trocada

static void logMemory(const char *screen) {
  Serial.printf("[mem] tela=%s heap_interno_livre=%u maior_bloco=%u psram_livre=%u\n", screen,
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

// Carrega a nova tela e apaga a anterior. A exclusão é adiada (async) porque normalmente
// estamos dentro do evento de clique de um botão que pertence à tela antiga.
// `refresh` (opcional) é chamado agora e a cada 2 s enquanto a tela estiver ativa.
static void loadScreen(lv_obj_t *scr, const char *name, lv_timer_cb_t refresh = nullptr,
                       uint32_t refreshMs = 2000) {
  if (onLeave) {
    onLeave();
    onLeave = nullptr;
  }
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
    refreshTimer = lv_timer_create(refresh, refreshMs, nullptr);
  }
}

static lv_obj_t *createScreen() {
  batteryLabel = wifiLabel = infoLabel = nullptr;
  spContent = spTitle = spArtist = spDevice = spElapsed = spTotal = spMessage = spSlider = spPlayLabel = spArt =
      spNotice = nullptr;
  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x101010), 0);
  lv_obj_set_style_text_color(scr, lv_color_white(), 0);
  lv_obj_set_style_text_font(scr, &font_pt_16, 0);
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
  lv_obj_set_style_text_font(label, &font_pt_28, 0);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return btn;
}

static lv_obj_t *createInfoLabel(lv_obj_t *scr) {
  lv_obj_t *label = lv_label_create(scr);
  lv_obj_set_style_text_font(label, &font_pt_16, 0);
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
      lv_label_set_text(wifiLabel, LV_SYMBOL_WIFI " sem conexão");
      break;
  }
}

static void settingsEvent(lv_event_t *);
static void spotifyEvent(lv_event_t *);

static void gameEvent(lv_event_t *) {
  logMemory("jogo");
  gameStart();  // a partir do próximo loop() o jogo assume a tela
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
                        "2. A página abre sozinha. Se não abrir,\n"
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
  lv_obj_set_style_text_font(infoLabel, &font_pt_20, 0);
  lv_label_set_recolor(infoLabel, true);

  NetStatus n = netStatus();
  if (n.hasSavedWifi) {
    // Troca de rede: cancelar = reiniciar e voltar para a rede salva
    lv_obj_t *cancel = createButton(scr, "Cancelar", lv_color_hex(0x606060), 200, 60, restartEvent);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 20, -15);
  }
  lv_obj_t *back = createButton(scr, LV_SYMBOL_HOME " Início", lv_palette_main(LV_PALETTE_BLUE), 200, 60, homeEvent);
  lv_obj_align(back, LV_ALIGN_BOTTOM_RIGHT, -20, -15);

  loadScreen(scr, "wifi_setup", refreshWifiSetup);
}

// ---------- Configurações ----------

static void refreshSettings(lv_timer_t *) {
  NetStatus n = netStatus();
  String wifi = n.mode == NetMode::Connected ? n.ssid + " (conectado)" : n.ssid + " (sem conexão, tentando...)";
  String page = n.mode == NetMode::Connected ? "http://" + n.ip + "  ou  http://painel.local" : "-";
  String spotify;
  String expiry;
  int daysLeft;
  if (!n.spotifyConfigured) {
    spotify = "não configurado";
  } else if (spotifyNeedsReauth()) {
    spotify = "#E74C3C Reautorize o Spotify#";
  } else if (tokenExpiry(n.spotifyAuthDate, expiry, daysLeft)) {
    spotify = "autorizado em " + n.spotifyAuthDate + ", vale até " + expiry;
    if (daysLeft < 15) spotify += daysLeft < 0 ? "\n#E74C3C Expirou: renove a autorização#"
                                               : "\n#E74C3C Faltam " + String(daysLeft) + " dias: renove a autorização#";
  } else {
    spotify = "autorizado em " + n.spotifyAuthDate;
  }
  lv_label_set_text_fmt(infoLabel,
                        "Configurações\n\n"
                        "Wi-Fi: %s\n"
                        "Página: %s\n"
                        "PIN: #1DB954 %s#\n"
                        "Spotify: %s",
                        wifi.c_str(), page.c_str(), n.pin.c_str(), spotify.c_str());
}

static void changeWifiEvent(lv_event_t *) {
  netStartSetup();
  uiShowWifiSetup();
}

static void volumeEvent(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(e);
  int v = lv_slider_get_value(slider);
  if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    lv_label_set_text_fmt(label, "Volume: %d%%", v);
  } else {  // soltou: aplica, salva e toca um bipe de teste
    soundSetVolume(v);
    soundPlay(Snd::Beep);
  }
}

static void settingsLeave() {
  soundEnable(false);
}

static void settingsEvent(lv_event_t *) {
  if (netStatus().mode == NetMode::Setup) {
    uiShowWifiSetup();
    return;
  }
  lv_obj_t *scr = createScreen();
  infoLabel = createInfoLabel(scr);
  lv_obj_set_style_text_font(infoLabel, &font_pt_20, 0);
  lv_label_set_recolor(infoLabel, true);

  // Volume do alto-falante (codec ES8311), salvo na NVS
  lv_obj_t *volLabel = lv_label_create(scr);
  lv_label_set_text_fmt(volLabel, "Volume: %d%%", soundVolume());
  lv_obj_align(volLabel, LV_ALIGN_BOTTOM_LEFT, 20, -95);
  lv_obj_t *vol = lv_slider_create(scr);
  lv_slider_set_range(vol, 0, 100);
  lv_slider_set_value(vol, soundVolume(), LV_ANIM_OFF);
  lv_obj_set_size(vol, 280, 12);
  lv_obj_align(vol, LV_ALIGN_BOTTOM_RIGHT, -30, -98);
  lv_obj_set_ext_click_area(vol, 20);
  lv_obj_add_event_cb(vol, volumeEvent, LV_EVENT_VALUE_CHANGED, volLabel);
  lv_obj_add_event_cb(vol, volumeEvent, LV_EVENT_RELEASED, volLabel);

  lv_obj_t *change = createButton(scr, "Trocar Wi-Fi", lv_color_hex(0xE67E22), 200, 60, changeWifiEvent);
  lv_obj_align(change, LV_ALIGN_BOTTOM_LEFT, 20, -15);
  lv_obj_t *back = createButton(scr, LV_SYMBOL_LEFT " Voltar", lv_palette_main(LV_PALETTE_BLUE), 200, 60, homeEvent);
  lv_obj_align(back, LV_ALIGN_BOTTOM_RIGHT, -20, -15);

  soundEnable(true);  // para o bipe de teste do volume
  loadScreen(scr, "settings", refreshSettings);
  onLeave = settingsLeave;
}

// ---------- Spotify ----------

static bool sliderDragging = false;
static lv_img_dsc_t artDsc;
static uint16_t *artPixels = nullptr;  // buffer da capa exibida (PSRAM), liberado ao trocar/fechar

static String formatTime(uint32_t ms) {
  uint32_t s = ms / 1000;
  char buf[12];
  snprintf(buf, sizeof(buf), "%u:%02u", s / 60, s % 60);
  return buf;
}

// Só troca o texto se mudou (evita redesenhar e reiniciar a rolagem do título)
static void setTextIfChanged(lv_obj_t *label, const String &text) {
  if (text != lv_label_get_text(label)) lv_label_set_text(label, text.c_str());
}

static void setArt(uint16_t *pixels, int w, int h) {
  lv_img_cache_invalidate_src(&artDsc);
  free(artPixels);
  artPixels = pixels;
  if (!pixels) {
    lv_obj_add_flag(spArt, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  artDsc.header.always_zero = 0;
  artDsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  artDsc.header.w = w;
  artDsc.header.h = h;
  artDsc.data_size = w * h * 2;
  artDsc.data = (const uint8_t *)pixels;
  lv_img_set_src(spArt, &artDsc);
  lv_obj_clear_flag(spArt, LV_OBJ_FLAG_HIDDEN);
}

static void artDeleted(lv_event_t *) {
  lv_img_cache_invalidate_src(&artDsc);
  free(artPixels);
  artPixels = nullptr;
}

static void refreshSpotify(lv_timer_t *) {
  SpPlayback p = spotifyPlayback();
  const char *message = nullptr;
  String errorMessage;
  switch (p.state) {
    case SpState::Loading: message = "Carregando..."; break;
    case SpState::NotConfigured:
      message = "Spotify não configurado.\nToque em " LV_SYMBOL_SETTINGS " na tela inicial para ver\no endereço da página de configuração.";
      break;
    case SpState::NoWifi: message = "Sem Wi-Fi.\nTentando reconectar..."; break;
    case SpState::NeedsReauth: message = "Reautorize o Spotify.\nVeja " LV_SYMBOL_SETTINGS " na tela inicial."; break;
    case SpState::Error:
      errorMessage = p.error + "\nTentando de novo...";
      message = errorMessage.c_str();
      break;
    case SpState::NoDevice: message = "Nenhum dispositivo tocando.\nComece a tocar no celular ou no computador."; break;
    case SpState::Ok: break;
  }
  if (message) {
    setTextIfChanged(spMessage, message);
    lv_obj_clear_flag(spMessage, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(spContent, LV_OBJ_FLAG_HIDDEN);
    setTextIfChanged(spDevice, "");
    return;
  }
  lv_obj_add_flag(spMessage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(spContent, LV_OBJ_FLAG_HIDDEN);

  setTextIfChanged(spTitle, p.title);
  setTextIfChanged(spArtist, p.artist);
  setTextIfChanged(spDevice, String(LV_SYMBOL_AUDIO " ") + p.device);
  setTextIfChanged(spPlayLabel, p.isPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

  // Entre as consultas (1 s) a barra anda sozinha; enquanto o dedo arrasta, o slider manda
  uint32_t progress = p.progressMs + (p.isPlaying ? millis() - p.fetchedAt : 0);
  if (progress > p.durationMs) progress = p.durationMs;
  if (!sliderDragging) {
    lv_slider_set_range(spSlider, 0, p.durationMs > 0 ? p.durationMs / 1000 : 1);
    lv_slider_set_value(spSlider, progress / 1000, LV_ANIM_OFF);
    setTextIfChanged(spElapsed, formatTime(progress));
  }
  setTextIfChanged(spTotal, formatTime(p.durationMs));

  bool showNotice = p.notice.length() && millis() - p.noticeAt < 3000;
  setTextIfChanged(spNotice, showNotice ? p.notice : "");

  uint16_t *pixels;
  int w, h;
  if (spotifyTakeArt(pixels, w, h)) setArt(pixels, w, h);
}

static void playPauseEvent(lv_event_t *) {
  spotifyCommand(spotifyPlayback().isPlaying ? SpCmd::Pause : SpCmd::Play);
  refreshSpotify(nullptr);  // ícone muda na hora
}

static void nextEvent(lv_event_t *) {
  spotifyCommand(SpCmd::Next);
}

static void previousEvent(lv_event_t *) {
  spotifyCommand(SpCmd::Previous);
}

static void sliderEvent(lv_event_t *e) {
  switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
      sliderDragging = true;
      break;
    case LV_EVENT_VALUE_CHANGED:
      if (sliderDragging) setTextIfChanged(spElapsed, formatTime(lv_slider_get_value(spSlider) * 1000));
      break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
      if (sliderDragging) {
        sliderDragging = false;
        spotifyCommand(SpCmd::Seek, lv_slider_get_value(spSlider) * 1000);
      }
      break;
    default:
      break;
  }
}

static void spotifyLeave() {
  spotifySetActive(false);
}

static void queueEvent(lv_event_t *);

static lv_obj_t *createLabel(lv_obj_t *parent, const lv_font_t *font, lv_coord_t w, lv_label_long_mode_t mode) {
  lv_obj_t *label = lv_label_create(parent);
  if (font) lv_obj_set_style_text_font(label, font, 0);
  if (w) lv_obj_set_width(label, w);
  lv_label_set_long_mode(label, mode);
  lv_label_set_text(label, "");
  return label;
}

static void spotifyEvent(lv_event_t *) {
  lv_obj_t *scr = createScreen();
  sliderDragging = false;

  // Tudo que some junto quando não há música (mensagem no lugar)
  spContent = lv_obj_create(scr);
  lv_obj_remove_style_all(spContent);
  lv_obj_set_size(spContent, 480, 320);
  lv_obj_clear_flag(spContent, LV_OBJ_FLAG_SCROLLABLE);

  // Capa: quadrado com ícone de música por baixo da imagem
  lv_obj_t *artBox = lv_obj_create(spContent);
  lv_obj_remove_style_all(artBox);
  lv_obj_set_size(artBox, 150, 150);
  lv_obj_set_style_bg_color(artBox, lv_color_hex(0x303030), 0);
  lv_obj_set_style_bg_opa(artBox, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(artBox, 8, 0);
  lv_obj_set_pos(artBox, 15, 70);
  lv_obj_t *artIcon = createLabel(artBox, &font_pt_28, 0, LV_LABEL_LONG_WRAP);
  lv_label_set_text(artIcon, LV_SYMBOL_AUDIO);
  lv_obj_center(artIcon);
  spArt = lv_img_create(artBox);
  lv_obj_center(spArt);
  lv_obj_add_flag(spArt, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(spArt, artDeleted, LV_EVENT_DELETE, nullptr);

  // Coluna da direita: título, artista, controles
  spTitle = createLabel(spContent, &font_pt_28, 275, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_pos(spTitle, 185, 72);
  spArtist = createLabel(spContent, &font_pt_20, 275, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(spArtist, lv_color_hex(0xB3B3B3), 0);
  lv_obj_set_pos(spArtist, 185, 112);

  lv_obj_t *prev = createButton(spContent, LV_SYMBOL_PREV, lv_color_hex(0x404040), 70, 60, previousEvent);
  lv_obj_set_pos(prev, 187, 158);
  lv_obj_t *play = createButton(spContent, LV_SYMBOL_PLAY, lv_color_hex(0x1DB954), 90, 60, playPauseEvent);
  lv_obj_set_pos(play, 277, 158);
  spPlayLabel = lv_obj_get_child(play, 0);
  lv_obj_t *next = createButton(spContent, LV_SYMBOL_NEXT, lv_color_hex(0x404040), 70, 60, nextEvent);
  lv_obj_set_pos(next, 387, 158);

  spNotice = createLabel(spContent, nullptr, 450, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(spNotice, lv_color_hex(0xE67E22), 0);
  lv_obj_set_style_text_align(spNotice, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(spNotice, 15, 228);

  // Slider de posição (arrastável) e tempos
  spSlider = lv_slider_create(spContent);
  lv_obj_set_size(spSlider, 430, 10);
  lv_obj_set_pos(spSlider, 25, 262);
  lv_obj_set_style_bg_color(spSlider, lv_color_hex(0x404040), LV_PART_MAIN);
  lv_obj_set_style_bg_color(spSlider, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(spSlider, lv_color_white(), LV_PART_KNOB);
  lv_obj_set_ext_click_area(spSlider, 20);  // mais fácil de pegar com o dedo
  lv_obj_add_event_cb(spSlider, sliderEvent, LV_EVENT_ALL, nullptr);

  spElapsed = createLabel(spContent, nullptr, 0, LV_LABEL_LONG_CLIP);
  lv_obj_set_pos(spElapsed, 15, 288);
  spTotal = createLabel(spContent, nullptr, 0, LV_LABEL_LONG_CLIP);
  lv_obj_align(spTotal, LV_ALIGN_TOP_RIGHT, -15, 288);

  // Topo: voltar e aparelho tocando (fora do spContent: sempre visíveis)
  lv_obj_t *back = createButton(scr, LV_SYMBOL_LEFT, lv_palette_main(LV_PALETTE_BLUE), 70, 50, homeEvent);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, 10, 8);
  lv_obj_t *queueBtn = createButton(scr, LV_SYMBOL_LIST, lv_color_hex(0x404040), 70, 50, queueEvent);
  lv_obj_align(queueBtn, LV_ALIGN_TOP_RIGHT, -10, 8);
  spDevice = createLabel(scr, nullptr, 300, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(spDevice, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_color(spDevice, lv_color_hex(0x1DB954), 0);
  lv_obj_align(spDevice, LV_ALIGN_TOP_RIGHT, -90, 24);

  spMessage = createLabel(scr, &font_pt_20, 440, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(spMessage, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(spMessage, LV_ALIGN_CENTER, 0, 20);

  spotifySetActive(true);
  loadScreen(scr, "spotify", refreshSpotify, 250);
  onLeave = spotifyLeave;
}

// ---------- Fila ----------

static lv_obj_t *queueList = nullptr, *queueMessage = nullptr;
static uint32_t queueVersionShown = 0;
static uint32_t queueRequestedAt = 0;
static const uint32_t QUEUE_REFRESH_MS = 10000;

static void refreshQueue(lv_timer_t *) {
  if (millis() - queueRequestedAt > QUEUE_REFRESH_MS) {
    queueRequestedAt = millis();
    spotifyRequestQueue(false);
  }
  SpQueue q = spotifyQueue();
  if (q.version == queueVersionShown) return;
  queueVersionShown = q.version;

  const char *message = !q.loaded        ? "Carregando fila..."
                        : q.failed       ? "Não foi possível carregar a fila.\nTentando de novo..."
                        : q.items.empty() ? "A fila está vazia."
                                         : nullptr;
  if (message) {
    lv_label_set_text(queueMessage, message);
    lv_obj_clear_flag(queueMessage, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(queueList, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_add_flag(queueMessage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(queueList, LV_OBJ_FLAG_HIDDEN);

  // Mantém a rolagem ao atualizar
  lv_coord_t scrollY = lv_obj_get_scroll_y(queueList);
  lv_obj_clean(queueList);
  int n = 1;
  for (const SpQueueItem &item : q.items) {
    lv_obj_t *row = lv_obj_create(queueList);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(row, 6, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x303030), 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);  // o toque rola a lista
    lv_obj_t *title = createLabel(row, &font_pt_20, 440, LV_LABEL_LONG_DOT);
    lv_label_set_text_fmt(title, "%d. %s", n++, item.title.c_str());
    lv_obj_t *artist = createLabel(row, nullptr, 440, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(artist, lv_color_hex(0xB3B3B3), 0);
    lv_label_set_text(artist, item.artist.c_str());
    lv_obj_set_pos(artist, 0, 26);
  }
  lv_obj_update_layout(queueList);
  lv_obj_scroll_to_y(queueList, scrollY, LV_ANIM_OFF);
}

static void queueBackEvent(lv_event_t *) {
  onLeave = nullptr;  // volta ao Spotify sem fechar as conexões
  spotifyEvent(nullptr);
}

static void queueEvent(lv_event_t *) {
  onLeave = nullptr;  // a consulta continua ativa enquanto a fila está aberta
  lv_obj_t *scr = createScreen();

  lv_obj_t *back = createButton(scr, LV_SYMBOL_LEFT, lv_palette_main(LV_PALETTE_BLUE), 70, 50, queueBackEvent);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, 10, 8);
  lv_obj_t *title = createLabel(scr, &font_pt_28, 0, LV_LABEL_LONG_CLIP);
  lv_label_set_text(title, "Fila");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 100, 16);

  queueList = lv_obj_create(scr);
  lv_obj_remove_style_all(queueList);
  lv_obj_set_size(queueList, 460, 250);
  lv_obj_align(queueList, LV_ALIGN_TOP_LEFT, 15, 66);
  lv_obj_set_flex_flow(queueList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(queueList, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(queueList, LV_SCROLLBAR_MODE_ACTIVE);

  queueMessage = createLabel(scr, &font_pt_20, 440, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(queueMessage, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(queueMessage, LV_ALIGN_CENTER, 0, 20);

  spotifyRequestQueue(true);
  queueRequestedAt = millis();
  queueVersionShown = UINT32_MAX;
  loadScreen(scr, "fila", refreshQueue, 250);
  onLeave = spotifyLeave;
}
