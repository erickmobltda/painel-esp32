#pragma once

#include <lvgl.h>

// Montserrat com Latin-1 (acentos) + símbolos do LVGL, gerada com lv_font_conv 1.5.2
// (mesmos parâmetros das fontes embutidas do LVGL 8.4, faixa 0x20-0x7E,0xA0-0xFF + pontuação).
LV_FONT_DECLARE(font_pt_16);
LV_FONT_DECLARE(font_pt_20);
LV_FONT_DECLARE(font_pt_28);
// Só A-Z e as letras acentuadas maiúsculas do português, 64 px: a palavra do app Ler
LV_FONT_DECLARE(font_le_64);
