#include "idle.h"
#include "display.h"
#include "power.h"
#include "spotify.h"
#include "game.h"
#include "sound.h"
#include "board.h"

#include <Arduino.h>
#include <LittleFS.h>

static const uint32_t DIM_AFTER_MS = 60000;
static const uint32_t OFF_AFTER_MS = 300000;
static const uint32_t SHUTDOWN_AFTER_MS = 600000;  // parada na bateria: desliga a placa (decisão do usuário)
static const uint8_t BRIGHT = 255, DIM = 40;
static const uint32_t USB_CHECK_MS = 1000;

enum class Screen { On, Dim, Off };
static Screen screen = Screen::On;
static bool onUsb = true;
static uint32_t lastUsbCheck = 0, lastActivity = 0;
static bool simulateBattery = false;

// Caixa-preta: eventos de energia gravados na flash para diagnosticar o uso na bateria (sem serial)
static const char *DIAG_PATH = "/diag.txt";
static const size_t DIAG_MAX = 8192;
void idleDiag(const char *fmt, ...) {
  char line[120];
  int n = snprintf(line, sizeof(line), "%lus ", millis() / 1000);
  va_list args;
  va_start(args, fmt);
  vsnprintf(line + n, sizeof(line) - n, fmt, args);
  va_end(args);
  // RAM interna livre / maior bloco: para entender travamentos por falta de memória (como o de 2026-10-20)
  n = strlen(line);
  snprintf(line + n, sizeof(line) - n, " | heap %u/%u", heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
           heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024);
  Serial.printf("[diag] %s\n", line);
  File f = LittleFS.open(DIAG_PATH, "a");
  if (!f) return;
  if (f.size() > DIAG_MAX) {  // cheio: recomeça (perde o mais antigo)
    f.close();
    f = LittleFS.open(DIAG_PATH, "w");
  }
  f.println(line);
  f.close();
}
void idleDiagDump(bool clear) {
  if (clear) {
    LittleFS.remove(DIAG_PATH);
    Serial.println("[diag] apagado");
    return;
  }
  File f = LittleFS.open(DIAG_PATH, "r");
  Serial.println("[diag] ----- inicio -----");
  while (f && f.available()) Serial.write(f.read());
  Serial.println("[diag] ----- fim -----");
  f.close();
}
// Toque rápido no PWR apaga/acende a tela (pressão longa é do AXP2101: desliga a placa)
static const uint32_t PWR_CLICK_MAX_MS = 1000;
static bool forcedOff = false;
static bool pwrDown = false;
static uint32_t pwrDownAt = 0;
static bool pwrClicked() {
  bool down = digitalRead(PIN_PWR_BUTTON) == HIGH;
  bool clicked = false;
  if (down && !pwrDown) pwrDownAt = millis();
  if (!down && pwrDown && millis() - pwrDownAt < PWR_CLICK_MAX_MS) clicked = true;
  pwrDown = down;
  return clicked;
}

static const char *reason = "inicio";  // o que manteve/acendeu a tela por último
static int16_t reasonX = 0, reasonY = 0;

static void setScreen(Screen s) {
  if (s == screen) return;
  screen = s;
  displaySetBrightness(s == Screen::On ? BRIGHT : s == Screen::Dim ? DIM : 0);
  displaySetSleeping(s == Screen::Off);
  spotifySetSuspended(s == Screen::Off);
  if (s == Screen::Off && gameRunning()) soundPlay(Snd::StopMusic);  // título/game over: não toca no escuro
  if (s == Screen::On) idleDiag("tela acesa por %s (%d,%d)", reason, reasonX, reasonY);
  else idleDiag("tela %s", s == Screen::Dim ? "escurecida" : "apagada");
}

void idleBegin() {
  pinMode(PIN_PWR_BUTTON, INPUT);
  lastActivity = millis();
  idleDiag("boot, motivo do reinicio %d", (int)esp_reset_reason());
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
    bool usb = (p.vbusIn || !p.batteryConnected) && !simulateBattery;  // sem bateria só pode estar no USB
    if (usb != onUsb)
      idleDiag("%s (vbus %d, bateria %d, %d%%, %umV)", usb ? "USB" : "BATERIA", p.vbusIn, p.batteryConnected,
               p.batteryPercent, p.batteryMv);
    onUsb = usb;
  }
  if (screen == Screen::Off) {
    // O LVGL e o jogo param de ler o touch: lê aqui para acordar (o toque é engolido pelo display)
    int16_t x, y;
    displayReadTouch(&x, &y, 1);
  }
  uint32_t touch = displayLastTouch();
  if (pwrClicked()) {
    forcedOff = !forcedOff && screen != Screen::Off;  // apagar se acesa/escurecida; acender se apagada
    lastActivity = now;
    reason = "botao PWR";
    idleDiag("botao PWR: %s", forcedOff ? "apagar" : "acender");
  }
  if (forcedOff && (int32_t)(touch - lastActivity) > 0) forcedOff = false;  // toque na tela acende
  if ((int32_t)(touch - lastActivity) > 0) {
    lastActivity = touch;
    reason = "toque";
    displayLastTouchPoint(reasonX, reasonY);
  }
  if (onUsb || gamePlaying()) {
    lastActivity = now;
    reason = onUsb ? "USB" : "jogo";
  }

  uint32_t idle = now - lastActivity;
  // Tela apagada não economiza o bastante (~10%/h com Wi-Fi e processador ligados): na bateria, desliga de vez
  if (!onUsb && idle >= SHUTDOWN_AFTER_MS) {
    idleDiag("desligando apos %u min parada na bateria", SHUTDOWN_AFTER_MS / 60000);
    delay(100);
    powerShutdown();
  }
  setScreen(forcedOff || idle >= OFF_AFTER_MS ? Screen::Off : idle >= DIM_AFTER_MS ? Screen::Dim : Screen::On);
  return screen != Screen::Off;
}
