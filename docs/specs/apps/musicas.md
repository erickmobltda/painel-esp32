# App Músicas

Código: `ui.cpp` (tela, `uiShowMusic`) + `spotify.cpp` (dados, tarefa `spotify`). Serial: `m` abre.

## O que a criança vê
- Grade de **8 capas** (4×2, 112 px), uma por música da playlist, sobre fundo rosa claro.
- Barra de baixo: ⌂ (início) · ◀ página anterior · ⏯ tocar/pausar · ▶ próxima página.
- Tocar numa capa: círculo de carregamento e moldura amarela na hora; a música começa no aparelho e a capa que está
  tocando ganha **moldura verde**. ⏯ muda o ícone na hora (estado otimista, corrigido pela próxima consulta).
- Avisos numa faixa azul-escura com texto amarelo: "Nenhum aparelho" quando não há onde tocar e erros do Spotify.
- Capa ainda não baixada aparece como ♪.

## Regras
- Playlist configurada na página da placa (NVS `kid_playlist`). Precisa ser **da conta autorizada**: o Spotify só
  devolve as músicas de playlists da própria conta (`GET /v1/playlists/{id}/items`).
- Tocar = `PUT /v1/me/player/play` com `context_uri` da playlist e `offset.uri` da música (segue tocando a playlist).
- **Aparelho:** com um aparelho ativo, toca nele. Sem aparelho ativo (404): `GET /v1/me/player/devices` e escolhe o
  primeiro que **não** for "Speaker" (celular/computador com o app aberto); se só houver Speaker, a Alexa; lista vazia →
  aviso. Cada conta só enxerga os próprios aparelhos.
- Com a tela aberta, consulta `GET /v1/me/player` a cada 1 s para a moldura verde e o ⏯. Fora dela, nada é consultado,
  mas o token é renovado em segundo plano quando falta menos de 5 min para vencer (o primeiro toque fica mais rápido).
- 401 → renova o token; 429 → espera o `Retry-After`; `invalid_grant` → "Reautorize o Spotify" e para de tentar.

## Capas e cache
- Lista em cache em `/musicas.txt` (abre na hora; atualiza pela internet ao entrar).
- Capa: a de ~300 px de `i.scdn.co`, decodificada (TJpgDec, escala 1/2) e reduzida para 112×112 RGB565 →
  `/capas/<albumId>.565` na LittleFS (~28 KB cada, 13 MB disponíveis). Para de gravar com menos de 64 KB livres.
- **Uma conexão HTTPS por vez** (duas não cabem na RAM interna): enquanto faltam capas, a API fica fechada; um toque
  para tocar ou um comando tem prioridade sobre o download.

## Segurança
HTTPS validado com o pacote de certificados do core (`setCACertBundle`), nunca `setInsecure()`. Client Secret e refresh
token só na NVS (ou no `secrets.h`, fora do git).

## Tela "Spotify" (escondida)
Controle remoto completo (música, artista, capa 150 px, ⏮ ⏯ ⏭, barra de posição e fila). Sem botão na tela inicial desde
a v1.1; o código continua em `ui.cpp` (`spotifyEvent`).
