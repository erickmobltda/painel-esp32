#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

#include "board.h"
#include "display.h"
#include "power.h"
#include "net.h"
#include "spotify.h"
#include "ui.h"
#include "game.h"
#include "sound.h"

void setup() {
  Serial.begin(115200);
  Serial.printf("\n[painel] iniciando (motivo do reinicio: %d)\n", esp_reset_reason());

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (!displayInit()) {
    Serial.println("[painel] falha na tela, parado");
    while (true) delay(1000);
  }
  powerInit();  // sem PMU a tela ainda funciona, só sem bateria
  soundBegin();

  netBegin();
  spotifyBegin();
  if (netStatus().mode == NetMode::Setup) uiShowWifiSetup();
  else uiShowHome();
  Serial.println("[painel] pronto");
}

void loop() {
  if (gameRunning()) {
    if (!gameFrame()) {
      gameStop();
      uiShowHome();
    }
    return;
  }
  lv_timer_handler();
  netLoop();

  // Depuração pelo serial: 's' liga/desliga a consulta ao Spotify sem abrir a tela; 'q' busca a fila; 'g' abre o jogo
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 's') {
      static bool debugActive = false;
      debugActive = !debugActive;
      Serial.printf("[debug] spotify ativo=%d\n", debugActive);
      spotifySetActive(debugActive);
    } else if (c == 'q') {
      spotifyRequestQueue(true);
    } else if (c == 'g') {
      gameStart();
    }
  }
  delay(5);
}
