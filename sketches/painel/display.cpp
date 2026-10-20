#include "display.h"
#include "board.h"

#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include "TCA9554.h"
#include "TouchDrvFT6X36.hpp"

static TCA9554 tca(TCA9554_ADDR);
// Barramento com DMA: o jogo envia a tela inteira a cada quadro (32 ms a 80 MHz, contra 84 ms no barramento
// sem DMA a 40 MHz). Sem MISO: o driver SPI do ESP-IDF limita a 26,7 MHz quando o MISO está configurado.
static Arduino_DataBus *bus = new Arduino_ESP32SPIDMA(PIN_LCD_DC, PIN_LCD_CS, PIN_SPI_SCK, PIN_SPI_MOSI, GFX_NOT_DEFINED);
// Rotação 1 = paisagem 480x320 (placa deitada). O touch continua no sistema nativo (retrato 320x480).
static Arduino_GFX *gfx = new Arduino_ST7796(bus, GFX_NOT_DEFINED /* RST */, LCD_ROTATION, true /* IPS */);
static TouchDrvFT6X36 touch;

static lv_disp_draw_buf_t drawBuf;
static lv_disp_drv_t dispDrv;
static lv_indev_drv_t indevDrv;

static void flushCb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#else
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#endif
  lv_disp_flush_ready(drv);
}

// Converte do sistema nativo do touch (retrato) para a rotação da tela
static void mapTouch(int16_t &x, int16_t &y) {
  int16_t nx = x, ny = y;
#if LCD_ROTATION == 1
  x = ny;
  y = LCD_NATIVE_WIDTH - 1 - nx;
#elif LCD_ROTATION == 3
  x = LCD_NATIVE_HEIGHT - 1 - ny;
  y = nx;
#endif
}

uint8_t displayReadTouch(int16_t *x, int16_t *y, uint8_t max) {
  uint8_t n = touch.getPoint(x, y, max);
  if (n > max) n = max;
  for (uint8_t i = 0; i < n; i++) mapTouch(x[i], y[i]);
  return n;
}

void displaySetAmplifier(bool on) {
  tca.write1(TCA_PIN_AMP, on ? 1 : 0);
}

Arduino_GFX *displayGfx() {
  return gfx;
}

static void touchReadCb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  int16_t x[1], y[1];
  if (displayReadTouch(x, y, 1)) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x[0];
    data->point.y = y[0];
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

bool displayInit() {
  // Reset do LCD e do touch pelo expansor TCA9554
  tca.begin();
  tca.pinMode1(TCA_PIN_LCD_RST, OUTPUT);
  tca.pinMode1(TCA_PIN_TOUCH_RST, OUTPUT);
  tca.pinMode1(TCA_PIN_AMP, OUTPUT);
  tca.write1(TCA_PIN_AMP, 0);
  tca.write1(TCA_PIN_LCD_RST, 1);
  tca.write1(TCA_PIN_TOUCH_RST, 1);
  delay(10);
  tca.write1(TCA_PIN_LCD_RST, 0);
  tca.write1(TCA_PIN_TOUCH_RST, 0);
  delay(10);
  tca.write1(TCA_PIN_LCD_RST, 1);
  tca.write1(TCA_PIN_TOUCH_RST, 1);
  delay(200);

  // 80 MHz: os pinos da tela são os nativos da VSPI (IO_MUX), que suportam essa velocidade.
  // O padrão da biblioteca é 40 MHz; o jogo precisa do dobro para enviar a tela inteira a cada quadro.
  if (!gfx->begin(80000000)) {
    Serial.println("[display] gfx->begin() falhou");
    return false;
  }
  gfx->fillScreen(RGB565_BLACK);
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);

  if (!touch.begin(Wire, FT6X36_SLAVE_ADDRESS)) {
    Serial.println("[display] touch FT6X36 nao encontrado");
    return false;
  }

  lv_init();

  uint32_t w = gfx->width();
  uint32_t h = gfx->height();
  // Um único buffer de 40 linhas na RAM interna: o envio pela SPI é bloqueante (flush_ready logo após
  // desenhar), então um segundo buffer nunca seria usado em paralelo e só gastaria RAM interna (o TLS precisa dela).
  uint32_t bufSize = w * 40;
  lv_color_t *buf = (lv_color_t *)heap_caps_malloc(bufSize * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf) {
    Serial.println("[display] falha ao alocar buffer do LVGL");
    return false;
  }
  lv_disp_draw_buf_init(&drawBuf, buf, nullptr, bufSize);

  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = w;
  dispDrv.ver_res = h;
  dispDrv.flush_cb = flushCb;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);

  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchReadCb;
  lv_indev_drv_register(&indevDrv);

  Serial.printf("[display] ok %ux%u\n", w, h);
  return true;
}
