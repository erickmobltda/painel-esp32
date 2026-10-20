#include "idle.h"
#include "display.h"
#include "power.h"
#include "spotify.h"
#include "game.h"
#include "sound.h"

#include <Arduino.h>

static const uint32_t DIM_AFTER_MS = 60000;
static const uint32_t OFF_AFTER_MS = 300000;
static const uint8_t BRIGHT = 255, DIM = 40;
static const uint32_t USB_CHECK_MS = 1000;

enum class Screen { On, Dim, Off };
static Screen screen = Screen::On;
static bool onUsb = true;
static uint32_t lastUsbCheck = 0, lastActivity = 0;
static bool simulateBattery = false;

static void setScreen(Screen s) {
  if (s == screen) return;
  screen = s;
  displaySetBrightness(s == Screen::On ? BRIGHT : s == Screen::Dim ? DIM : 0);
  displaySetSleeping(s == Screen::Off);
  spotifySetSuspended(s == Screen::Off);
  if (s == Screen::Off && gameRunning()) soundPlay(Snd::StopMusic);  // título/game over: não toca no escuro
  Serial.printf("[tela] %s\n", s == Screen::On ? "acesa" : s == Screen::Dim ? "escurecida" : "apagada");
}

void idleBegin() {
  lastActivity = millis();
}

void idleSimulateBattery(bool on) {
  simulateBattery = on;
  lastUsbCheck = 0;
}

bool idleLoop() {
  uint32_t now = millis();
  if (now - lastUsbCheck >= USB_CHECK_MS) {
    lastUsbCheck = now;
    PowerStatus p = powerRead();
    onUsb = (p.vbusIn || !p.batteryConnected) && !simulateBattery;  // sem bateria só pode estar no USB
  }
  if (screen == Screen::Off) {
    // O LVGL e o jogo param de ler o touch: lê aqui para acordar (o toque é engolido pelo display)
    int16_t x, y;
    displayReadTouch(&x, &y, 1);
  }
  uint32_t touch = displayLastTouch();
  if ((int32_t)(touch - lastActivity) > 0) lastActivity = touch;
  if (onUsb || gamePlaying()) lastActivity = now;

  uint32_t idle = now - lastActivity;
  setScreen(idle >= OFF_AFTER_MS ? Screen::Off : idle >= DIM_AFTER_MS ? Screen::Dim : Screen::On);
  return screen != Screen::Off;
}
