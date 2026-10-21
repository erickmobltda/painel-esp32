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
// Brilho da luz de fundo (0-255, PWM no pino 25).
void displaySetBrightness(uint8_t level);
// millis() do último toque (qualquer tela, inclusive o jogo).
uint32_t displayLastTouch();
void displayLastTouchPoint(int16_t &x, int16_t &y);  // coordenada nativa do último toque (diagnóstico)
// Tela apagada: o próximo toque só acorda (é ignorado até o dedo sair, para não apertar nada sem querer).
void displaySetSleeping(bool sleeping);
// Amplificador do alto-falante (pino do expansor TCA9554, que este módulo já controla).
void displaySetAmplifier(bool on);
