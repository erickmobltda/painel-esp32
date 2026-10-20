#define XPOWERS_CHIP_AXP2101
#include "power.h"
#include "board.h"

#include <Wire.h>
#include "XPowersLib.h"

static XPowersPMU pmu;
static bool pmuOk = false;

bool powerInit() {
  pmuOk = pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL);
  if (!pmuOk) {
    Serial.println("[power] AXP2101 nao encontrado");
    return false;
  }
  // Apenas liga as medições do ADC (mesmas chamadas do exemplo 02_axp2101_example)
  pmu.enableBattDetection();
  pmu.enableVbusVoltageMeasure();
  pmu.enableBattVoltageMeasure();
  pmu.enableSystemVoltageMeasure();
  Serial.printf("[power] AXP2101 ok, id 0x%x\n", pmu.getChipID());
  return true;
}

PowerStatus powerRead() {
  PowerStatus s = {false, -1, 0, false, false};
  if (!pmuOk) return s;
  s.batteryConnected = pmu.isBatteryConnect();
  s.batteryPercent = pmu.getBatteryPercent();
  s.batteryMv = pmu.getBattVoltage();
  s.vbusIn = pmu.isVbusIn();
  s.charging = pmu.isCharging();
  return s;
}
