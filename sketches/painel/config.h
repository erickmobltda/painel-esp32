#pragma once

#include <Arduino.h>

// Configuração persistente na NVS (Preferences). Se a NVS estiver vazia, usa os valores
// padrão de secrets.h (opcional, fora do git — ver secrets.example.h).
struct WifiNet {
  String ssid, pass;
};
static const int CONFIG_WIFI_MAX = 5;  // redes lembradas (casa, casa da avó...)
struct Config {
  WifiNet wifi[CONFIG_WIFI_MAX];  // [0] = a cadastrada por último
  int wifiCount = 0;
  String spClientId;
  String spClientSecret;
  String spRefreshToken;
  String spAuthDate;  // AAAA-MM-DD da autorização no Spotify
  String kidPlaylist; // id da playlist do app Músicas
  String childName;   // nome escrito no fim da sessão do Monte a Palavra (vazio = sem nome)
};

void configLoad(Config &cfg);
// Põe a rede na frente da lista (se já existia, troca a senha); com mais de CONFIG_WIFI_MAX esquece a mais antiga.
void configSaveWifi(const String &ssid, const String &pass);
void configSaveSpotify(const String &clientId, const String &clientSecret, const String &refreshToken,
                       const String &authDate);
// O Spotify pode devolver um refresh token novo ao renovar o acesso; a data da autorização não muda.
void configSaveRefreshToken(const String &refreshToken);
void configSaveKidPlaylist(const String &playlistId);
void configSaveChildName(const String &name);
