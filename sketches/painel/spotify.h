#pragma once

#include <Arduino.h>
#include <vector>
#include <esp_heap_caps.h>

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
  String trackUri;  // spotify:track:... (o app Músicas usa para marcar a capa que está tocando)
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

// ---------- App Músicas (playlist da criança) ----------
static const int SP_THUMB_SIZE = 112;  // lado das miniaturas gravadas na flash (RGB565 cru)
// Texto em tamanho fixo e a lista inteira na PSRAM: com String cada campo era um bloco pequeno na RAM interna
// (~8 KB por cópia da lista, e há até 3 cópias ao atualizar), que falta para as conexões TLS
struct SpPlaylistItem {
  char uri[48] = "";      // spotify:track:<22>
  char albumId[24] = "";  // <22>
  char artUrl[80] = "";   // https://i.scdn.co/image/<40>; vazio = sem capa
};
template <typename T> struct SpPsramAlloc {
  using value_type = T;
  SpPsramAlloc() = default;
  template <typename U> SpPsramAlloc(const SpPsramAlloc<U> &) {}
  T *allocate(size_t n) { return (T *)heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM); }
  void deallocate(T *p, size_t) { heap_caps_free(p); }
  template <typename U> bool operator==(const SpPsramAlloc<U> &) const { return true; }
  template <typename U> bool operator!=(const SpPsramAlloc<U> &) const { return false; }
};
struct SpPlaylist {
  bool loaded = false;  // tem itens (do cache ou da internet)
  bool failed = false;  // falhou e não há cache
  uint32_t version = 0;
  std::vector<SpPlaylistItem, SpPsramAlloc<SpPlaylistItem>> items;
};
// Capa grande só na tela do Spotify; o app Músicas desliga e no lugar baixa as miniaturas que faltam.
void spotifySetArtWanted(bool wanted);
// Carrega o cache da flash na hora e atualiza pela internet em seguida.
void spotifyRequestPlaylist(const String &id);
SpPlaylist spotifyPlaylist();
uint32_t spotifyThumbsVersion();  // muda a cada miniatura nova
uint32_t spotifyPlaylistVersion();  // muda quando a lista muda (evita copiar a lista à toa)
String spotifyThumbPath(const String &albumId);
// Toca a música dentro da playlist (e segue a playlist depois).
void spotifyPlayInPlaylist(const String &trackUri);

// Troca o "code" do login (fluxo pela página da placa) por um refresh token. Bloqueia ~1-2 s.
bool spotifyExchangeCode(const String &clientId, const String &clientSecret, const String &code,
                         String &refreshTokenOut, String &errorOut);
