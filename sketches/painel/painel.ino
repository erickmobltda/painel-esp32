#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

#include "board.h"
#include "display.h"
#include "power.h"
#include "net.h"
#include "spotify.h"
#include "ui.h"

void setup() {
  Serial.begin(115200);
  Serial.printf("\n[painel] iniciando (motivo do reinicio: %d)\n", esp_reset_reason());

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (!displayInit()) {
    Serial.println("[painel] falha na tela, parado");
    while (true) delay(1000);
  }
  powerInit();  // sem PMU a tela ainda funciona, só sem bateria

  netBegin();
  spotifyBegin();
  if (netStatus().mode == NetMode::Setup) uiShowWifiSetup();
  else uiShowHome();
  Serial.println("[painel] pronto");
}

void loop() {
  lv_timer_handler();
  netLoop();

  // Depuração pelo serial: 's' liga/desliga a consulta ao Spotify sem abrir a tela
  if (Serial.available() && Serial.read() == 's') {
    static bool debugActive = false;
    debugActive = !debugActive;
    Serial.printf("[debug] spotify ativo=%d\n", debugActive);
    spotifySetActive(debugActive);
  }
  delay(5);
}
