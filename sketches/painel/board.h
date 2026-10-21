#pragma once

// Pinos conferidos nos exemplos oficiais Waveshare (vendor/.../examples/Arduino/examples)
#define PIN_I2C_SDA 21
#define PIN_I2C_SCL 22

#define PIN_LCD_DC   27
#define PIN_LCD_CS   5
#define PIN_SPI_SCK  18
#define PIN_SPI_MOSI 23
#define PIN_SPI_MISO 19
#define PIN_LCD_BL   25

#define LCD_ROTATION      1    // paisagem
#define LCD_NATIVE_WIDTH  320
#define LCD_NATIVE_HEIGHT 480

#define TCA9554_ADDR      0x20
#define TCA_PIN_LCD_RST   0
#define TCA_PIN_TOUCH_RST 1
#define TCA_PIN_AMP       2    // liga o amplificador do alto-falante

// Áudio: codec ES8311 (I2C 0x18) + I2S (exemplos 01_i2s_audio / 04_es8311_example)
#define PIN_I2S_BCLK 2
#define PIN_I2S_LRCK 4
#define PIN_I2S_DOUT 12

#define PIN_PMU_IRQ 35
#define PIN_PWR_BUTTON 36  // botão PWR, nível alto quando apertado (exemplo oficial 03_button_example)
