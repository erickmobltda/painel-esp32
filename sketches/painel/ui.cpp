#include "ui.h"
#include "power.h"

#include <Arduino.h>
#include <lvgl.h>

static lv_obj_t *batteryLabel = nullptr;
static lv_obj_t *wifiLabel = nullptr;
static lv_timer_t *statusTimer = nullptr;

static void logMemory(const char *screen) {
  Serial.printf("[mem] tela=%s heap_interno_livre=%u maior_bloco=%u psram_livre=%u\n", screen,
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

// Carrega a nova tela e apaga a anterior. A exclusão é adiada (async) porque normalmente
// estamos dentro do evento de clique de um botão que pertence à tela antiga.
static void loadScreen(lv_obj_t *scr, const char *name) {
  if (statusTimer) {
    lv_timer_del(statusTimer);
    statusTimer = nullptr;
  }
  lv_obj_t *old = lv_scr_act();
  lv_scr_load(scr);
  if (old) lv_obj_del_async(old);
  logMemory(name);
}

static const char *batterySymbol(int percent) {
  if (percent >= 90) return LV_SYMBOL_BATTERY_FULL;
  if (percent >= 65) return LV_SYMBOL_BATTERY_3;
  if (percent >= 35) return LV_SYMBOL_BATTERY_2;
  if (percent >= 10) return LV_SYMBOL_BATTERY_1;
  return LV_SYMBOL_BATTERY_EMPTY;
}

static void updateStatus(lv_timer_t *) {
  if (!batteryLabel) return;
  PowerStatus p = powerRead();
  if (!p.batteryConnected) {
    lv_label_set_text(batteryLabel, LV_SYMBOL_USB " Sem bateria");
  } else if (p.charging) {
    lv_label_set_text_fmt(batteryLabel, LV_SYMBOL_CHARGE " %s %d%%", batterySymbol(p.batteryPercent), p.batteryPercent);
  } else {
    lv_label_set_text_fmt(batteryLabel, "%s %d%%%s", batterySymbol(p.batteryPercent), p.batteryPercent,
                          p.vbusIn ? " " LV_SYMBOL_USB : "");
  }
  Serial.printf("[power] bat=%d conectada=%d %umV usb=%d carregando=%d\n", p.batteryPercent, p.batteryConnected,
                p.batteryMv, p.vbusIn, p.charging);
}

static void backEvent(lv_event_t *) {
  uiShowHome();
}

// Tela provisória até as etapas 4 (Spotify) e 7 (Jogo).
static void showPlaceholder(const char *title) {
  lv_obj_t *scr = lv_obj_create(nullptr);

  lv_obj_t *label = lv_label_create(scr);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
  lv_label_set_text_fmt(label, "%s\n\nEm breve", title);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, -50);

  lv_obj_t *btn = lv_btn_create(scr);
  lv_obj_set_size(btn, 200, 70);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_add_event_cb(btn, backEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *btnLabel = lv_label_create(btn);
  lv_obj_set_style_text_font(btnLabel, &lv_font_montserrat_28, 0);
  lv_label_set_text(btnLabel, LV_SYMBOL_LEFT " Voltar");
  lv_obj_center(btnLabel);

  batteryLabel = nullptr;
  wifiLabel = nullptr;
  loadScreen(scr, title);
}

static void spotifyEvent(lv_event_t *) {
  showPlaceholder("Spotify");
}

static void gameEvent(lv_event_t *) {
  showPlaceholder("Jogo");
}

static lv_obj_t *createBigButton(lv_obj_t *parent, const char *text, lv_color_t color, lv_event_cb_t cb) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, 210, 250);
  lv_obj_set_style_bg_color(btn, color, 0);
  lv_obj_set_style_radius(btn, 20, 0);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *label = lv_label_create(btn);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return btn;
}

void uiShowHome() {
  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x101010), 0);

  // Barra de status no topo
  wifiLabel = lv_label_create(scr);
  lv_obj_set_style_text_color(wifiLabel, lv_color_hex(0x808080), 0);
  lv_label_set_text(wifiLabel, LV_SYMBOL_WIFI " --");  // Wi-Fi entra na etapa 3
  lv_obj_align(wifiLabel, LV_ALIGN_TOP_LEFT, 10, 8);

  batteryLabel = lv_label_create(scr);
  lv_obj_set_style_text_color(batteryLabel, lv_color_white(), 0);
  lv_label_set_text(batteryLabel, "");
  lv_obj_align(batteryLabel, LV_ALIGN_TOP_RIGHT, -10, 8);

  lv_obj_t *spotifyBtn = createBigButton(scr, LV_SYMBOL_AUDIO "  Spotify", lv_color_hex(0x1DB954), spotifyEvent);
  lv_obj_align(spotifyBtn, LV_ALIGN_BOTTOM_LEFT, 20, -20);

  lv_obj_t *gameBtn = createBigButton(scr, LV_SYMBOL_PLAY "  Jogo", lv_color_hex(0xE67E22), gameEvent);
  lv_obj_align(gameBtn, LV_ALIGN_BOTTOM_RIGHT, -20, -20);

  loadScreen(scr, "home");
  updateStatus(nullptr);
  statusTimer = lv_timer_create(updateStatus, 2000, nullptr);
}
