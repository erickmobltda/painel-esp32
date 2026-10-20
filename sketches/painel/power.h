#pragma once

#include <stdint.h>

struct PowerStatus {
  bool batteryConnected;
  int batteryPercent;   // -1 se sem bateria
  uint16_t batteryMv;
  bool vbusIn;          // USB conectado
  bool charging;
};

// PMU AXP2101: só leitura (não altera tensões). Requer Wire.begin() antes.
bool powerInit();
PowerStatus powerRead();
