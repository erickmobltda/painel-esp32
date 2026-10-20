#include "clock.h"

#include <time.h>

void clockBegin() {
  configTzTime("<-03>3", "pool.ntp.org", "time.google.com");
}

bool clockSynced() {
  return time(nullptr) > 1735689600;  // depois de 2025-01-01: já veio do NTP
}

String clockToday() {
  if (!clockSynced()) return "";
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  char buf[11];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
  return buf;
}

bool tokenExpiry(const String &authDate, String &expiryDateOut, int &daysLeftOut) {
  struct tm t = {};
  if (!clockSynced() || sscanf(authDate.c_str(), "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday) != 3) return false;
  t.tm_year -= 1900;
  t.tm_mon = t.tm_mon - 1 + 6;  // mês 0-based + 6 meses (mktime normaliza a virada de ano)
  t.tm_hour = 12;
  time_t expiry = mktime(&t);
  char buf[11];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
  expiryDateOut = buf;
  daysLeftOut = (int)((expiry - time(nullptr)) / 86400);
  return true;
}
