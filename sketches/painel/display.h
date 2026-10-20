#pragma once

#include <stdint.h>

// LCD ST7796 + touch FT6336 + LVGL 8 (inicialização do exemplo oficial 09_lvgl_arduino_v8).
// Requer Wire.begin() antes.
bool displayInit();

class Arduino_GFX;
// Acesso direto à tela para o jogo (que desenha sem o LVGL).
Arduino_GFX *displayGfx();
// Lê até `max` toques (o FT6336 suporta 2) já nas coordenadas da tela rotacionada. Devolve quantos.
uint8_t displayReadTouch(int16_t *x, int16_t *y, uint8_t max);
