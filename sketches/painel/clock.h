#pragma once

#include <Arduino.h>

// Hora pela internet (NTP), fuso de Brasília (UTC-3, sem horário de verão).
// Pode ser chamada antes do Wi-Fi conectar: o sincronismo acontece sozinho quando houver rede.
void clockBegin();
bool clockSynced();
// "AAAA-MM-DD" de hoje; vazio se o relógio ainda não sincronizou.
String clockToday();

// Refresh token do Spotify vale 6 meses a partir da autorização (renovar não estende).
// Devolve false se a data for inválida ou o relógio não estiver sincronizado.
bool tokenExpiry(const String &authDate, String &expiryDateOut, int &daysLeftOut);
