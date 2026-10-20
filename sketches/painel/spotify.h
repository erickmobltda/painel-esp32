#pragma once

#include <Arduino.h>

#define SPOTIFY_REDIRECT_URI "http://127.0.0.1:8888/callback"
#define SPOTIFY_SCOPES "user-read-playback-state user-read-currently-playing user-modify-playback-state"

enum class SpState {
  Loading,        // ainda não houve resposta
  NotConfigured,  // sem credenciais na NVS
  NoWifi,
  NeedsReauth,    // invalid_grant: refresh token inválido/expirado
  Error,          // falha temporária (rede, 5xx...): tenta de novo sozinho
  NoDevice,       // 204 / nada tocando em nenhum aparelho
  Ok
};

struct SpPlayback {
  SpState state = SpState::Loading;
  bool isPlaying = false;
  String title, artist, device;
  uint32_t progressMs = 0;
  uint32_t durationMs = 0;
  uint32_t fetchedAt = 0;  // millis() da resposta, para avançar a barra localmente
  String error;
};

// Cria a tarefa (núcleo 0) que conversa com a API. Requer configLoad/NVS já disponível.
void spotifyBegin();
// Consulta o player a cada ~1 s só enquanto a tela do Spotify estiver aberta.
void spotifySetActive(bool active);
SpPlayback spotifyPlayback();
// Relê credenciais da NVS (depois de salvar pela página/script) e sai de NeedsReauth.
void spotifyReloadConfig();
bool spotifyNeedsReauth();

// Troca o "code" do login (fluxo pela página da placa) por um refresh token. Bloqueia ~1-2 s.
bool spotifyExchangeCode(const String &clientId, const String &clientSecret, const String &code,
                         String &refreshTokenOut, String &errorOut);
