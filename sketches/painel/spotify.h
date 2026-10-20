#pragma once

#include <Arduino.h>
#include <vector>

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
  String notice;           // aviso curto de comando recusado (ex.: 403); some sozinho
  uint32_t noticeAt = 0;
};

enum class SpCmd { Play, Pause, Next, Previous, Seek };

struct SpQueueItem {
  String title, artist;
};

struct SpQueue {
  bool loaded = false;  // false = ainda buscando
  bool failed = false;
  uint32_t version = 0;  // muda a cada resposta (a tela só redesenha a lista quando muda)
  std::vector<SpQueueItem> items;
};

// Cria a tarefa (núcleo 1) que conversa com a API. Requer configLoad/NVS já disponível.
void spotifyBegin();
// Consulta o player a cada ~1 s só enquanto a tela do Spotify estiver aberta.
void spotifySetActive(bool active);
SpPlayback spotifyPlayback();
// Tela apagada: pausa as consultas sem sair da tela do Spotify (e retoma ao acender).
void spotifySetSuspended(bool suspended);
// Relê credenciais da NVS (depois de salvar pela página/script) e sai de NeedsReauth.
void spotifyReloadConfig();
bool spotifyNeedsReauth();

// Envia um comando ao player (assíncrono). Atualiza o estado local na hora e consulta logo depois.
void spotifyCommand(SpCmd cmd, uint32_t positionMs = 0);

// Capa do álbum (RGB565, ~150x150, na PSRAM). Devolve true se a capa mudou desde a última chamada;
// pixels == nullptr significa "sem capa". A posse do buffer passa para quem chamou (liberar com free()).
bool spotifyTakeArt(uint16_t *&pixels, int &w, int &h);

// Fila: pede uma busca nova (a tarefa responde em ~1 s) e lê o último resultado.
// clear = true descarta o resultado anterior (tela recém-aberta mostra "Carregando").
void spotifyRequestQueue(bool clear);
SpQueue spotifyQueue();

// Troca o "code" do login (fluxo pela página da placa) por um refresh token. Bloqueia ~1-2 s.
bool spotifyExchangeCode(const String &clientId, const String &clientSecret, const String &code,
                         String &refreshTokenOut, String &errorOut);
