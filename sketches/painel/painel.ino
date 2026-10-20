#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

#include "board.h"
#include "display.h"
#include "power.h"
#include "ui.h"

void setup() {
  Serial.begin(115200);
  Serial.println("\n[painel] iniciando");

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (!displayInit()) {
    Serial.println("[painel] falha na tela, parado");
    while (true) delay(1000);
  }
  powerInit();  // sem PMU a tela ainda funciona, só sem bateria

  uiShowHome();
  Serial.println("[painel] pronto");
}

void loop() {
  lv_timer_handler();
  delay(5);
}
