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
#include "idle.h"
#include "paint.h"

#include <LittleFS.h>

void setup() {
  Serial.begin(115200);
  Serial.printf("\n[painel] iniciando (motivo do reinicio: %d)\n", esp_reset_reason());

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (!displayInit()) {
    Serial.println("[painel] falha na tela, parado");
    while (true) delay(1000);
  }
  powerInit();  // sem PMU a tela ainda funciona, só sem bateria
  // Flash de arquivos (desenho do Pintar, capas do app Músicas); formata na 1ª vez
  if (!LittleFS.begin(true)) Serial.println("[painel] LittleFS indisponivel");
  else Serial.printf("[painel] LittleFS %u/%u KB\n", LittleFS.usedBytes() / 1024, LittleFS.totalBytes() / 1024);
  soundBegin();

  netBegin();
  spotifyBegin();
  if (netStatus().mode == NetMode::Setup) uiShowWifiSetup();
  else uiShowHome();
  idleBegin();
  Serial.println("[painel] pronto");
}

void loop() {
  if (!idleLoop()) {  // tela apagada: não desenha (só a rede segue funcionando)
    netLoop();
    delay(50);
    return;
  }
  if (paintRunning()) {
    if (!paintFrame()) {
      paintStop();
      uiShowHome();
    }
    return;
  }
  if (gameRunning()) {
    if (!gameFrame()) {
      gameStop();
      uiShowHome();
    }
    return;
  }
  lv_timer_handler();
  paintStartIfRequested();
  netLoop();

  // Depuração pelo serial: 's' liga/desliga a consulta ao Spotify sem abrir a tela; 'q' busca a fila; 'g' abre o jogo; 'b' simula bateria; 'p' abre o Pintar; 'm' abre o Músicas; 'i' mostra / 'I' apaga a caixa-preta de energia
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
    } else if (c == 'i' || c == 'I') {
      idleDiagDump(c == 'I');
    } else if (c == 'm') {
      uiShowMusic();
    } else if (c == 'p') {
      paintStart();
    } else if (c == 'b') {
      static bool battery = false;
      battery = !battery;
      Serial.printf("[debug] simular bateria=%d\n", battery);
      idleSimulateBattery(battery);
    }
  }
  delay(5);
}
