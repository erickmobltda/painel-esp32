#pragma once

#include <Arduino.h>

// Configuração persistente na NVS (Preferences). Se a NVS estiver vazia, usa os valores
// padrão de secrets.h (opcional, fora do git — ver secrets.example.h).
struct Config {
  String wifiSsid;
  String wifiPass;
  String spClientId;
  String spClientSecret;
  String spRefreshToken;
  String spAuthDate;  // AAAA-MM-DD da autorização no Spotify
};

void configLoad(Config &cfg);
void configSaveWifi(const String &ssid, const String &pass);
void configSaveSpotify(const String &clientId, const String &clientSecret, const String &refreshToken,
                       const String &authDate);
// O Spotify pode devolver um refresh token novo ao renovar o acesso; a data da autorização não muda.
void configSaveRefreshToken(const String &refreshToken);
