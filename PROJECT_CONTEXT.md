# PROJECT_CONTEXT — Painel (Waveshare ESP32-Touch-LCD-3.5)

Objetivo, regras e etapas completas: ver o briefing original (Spotify remote + Jogo do Pulo).

## Estado atual
- **Etapa 1 concluída (2026-10-20)**: `09_lvgl_arduino_v8` oficial, sem modificação, compilado e gravado;
  usuário confirmou na placa texto + switches + toque.
- **Etapa 2 concluída (2026-10-20)**: sketch `sketches/painel` com tela inicial em paisagem (Spotify | Jogo),
  barra de status (Wi-Fi placeholder, bateria %/carregando/USB). Usuário confirmou na placa: orientação, botões, voltar, ⚡ some sem USB.
- **Etapa 3 concluída (2026-10-20)**: Wi-Fi com ponto de acesso de cadastro, conexão, tela ⚙ e página de config com PIN.
  Usuário confirmou: cadastro pelo celular, reconexão após reiniciar, página na rede local, Trocar Wi-Fi/Cancelar.
- **Etapa 4 concluída (2026-10-20)**: app Spotify criado pelo usuário (Redirect URI `http://127.0.0.1:8888/callback`,
  usuário em User Management); autorização pela página da placa (fluxo principal) ou `tools/spotify_auth.py`;
  tela do Spotify mostra música/artista/aparelho/progresso. Usuário confirmou: tocar, pausar, trocar música, voltar, ⚙ com validade.
- **Etapa 5 concluída (2026-10-20)**: controles ⏮ ⏯ ⏭, slider de seek arrastável e **capa do álbum** (a etapa 10
  foi antecipada para cá por decisão do usuário). Usuário confirmou na placa.
- **Etapa 6 concluída (2026-10-20)**: fila (botão ☰ na tela do Spotify → tela "Fila", até 20 itens, nome + artista,
  rolagem por toque, atualiza a cada 10 s). Usuário confirmou na placa.
- (Tocar uma música da fila ao tocar nela: explicado ao usuário — pular N vezes ou `PUT /play` com contexto+offset — não incluído por ora.)
- **Etapa 7 concluída (2026-10-20)**: Capitã Maeva portada (gráficos + controles, sem som). ~20 FPS jogando. Usuário confirmou.
- **Etapa 8 concluída (2026-10-20)**: sons do jogo sintetizados na placa (música + efeitos), mudo no jogo, volume na ⚙.
  Usuário confirmou.
- **Etapa 9 — código pronto, TESTES DO USUÁRIO PENDENTES (2026-10-20)**: na bateria escurece após 1 min e apaga após 5 min
  (tempos escolhidos pelo usuário; não escurece durante a partida do jogo; com USB fica sempre acesa). Validado por mim via
  serial com bateria simulada (`b`): escureceu em 60 s e apagou em 5 min. Falta o usuário confirmar na placa:
  1) toque na tela apagada só acorda (não clica); 2) roteador desligado → "sem conexão"/"Sem Wi-Fi", sem travar, e
  reconecta sozinho ao religar; 3) nada tocando → mensagem clara; 4) teste real na bateria (sem USB).
  A conferir também: RAM ao sair do Spotify (log mostrou 69 KB livres logo na troca; verificar se volta ao normal após
  alguns segundos, quando a tarefa fecha as conexões TLS).
- **App "Pintar" (pedido extra do usuário, 2026-10-20) concluído**: confirmado na placa.
- **Release v1.0 publicado (2026-10-20).**
- **v1.2 (2026-10-20)**: fundo rosa claro (#FADDE8) na tela inicial e no Músicas (textos escuros); Músicas com círculo
  de carregamento ao tocar numa capa, ícone ⏯ e moldura amarela (6 px) que mudam na hora (estado otimista); toque mais
  sensível (limiar FT6336 = 3/4 do de fábrica: 60 → 45; modo econômico do touch só após 255 s); Pintar não quebra o traço
  em falhas de leitura < 60 ms. Usuário confirmou tudo.
- **v1.1 (2026-10-20): app "Músicas"** (confirmado pelo usuário). A tela "Spotify" (controle remoto) ficou escondida.
- (histórico) Ideia em discussão: app de músicas para a filha (capas favoritas → tocar na Alexa). Descoberto: a Alexa toca na
  **conta Spotify da filha** (cada conta só enxerga seus aparelhos) → provavelmente autorizar a conta dela (User Management).
  Teste: `GET /me/player/devices` na conta do usuário veio vazio com nada tocando.
- **Etapa 11 (README final) concluída (2026-10-21)**: `README.md` para o usuário (uso, botões, bateria, Wi-Fi, Spotify,
  renovação do token, playlist, gravação por release ou compilando, restauração de fábrica, diagnóstico).
- **Testes na placa (2026-10-21, usuário):** segurar PWR desliga e um toque rápido religa ✔; desligamento automático
  aos 10 min e religar pelo PWR ✔; toque que acorda não clica (escura e apagada) ✔; jogo não escurece ✔.
  Não feitos: Wi-Fi caindo (usuário não consegue desligar o roteador), noite na bateria (fará), `tools/spotify_auth.py` (depois).
  **Bug corrigido (v1.3):** Músicas sem aparelho ativo reiniciava a placa. Causa: `apiRequest` não chamava `http.end()`
  e o HTTPClient acumulava cabeçalhos; depois do PUT /play com corpo, todo pedido ia com `Content-Length: 140` → 400 do
  Google Front End para sempre, reconexões TLS seguidas, RAM interna esgotada (coredump: abort em `lock_init_generic`
  no log de erro do mDNS). Também corrigido: desligava aos 27 s (toque lido com tela apagada depois de `now` →
  `now - lastActivity` negativo em uint32).
  **Escolha do aparelho (decisão do usuário, 2026-10-21):** sem aparelho ativo, prefere celular/computador (só aparece
  com o app aberto), senão a Alexa (Speaker, aparece mesmo parada). Com aparelho ativo, o PUT /play vai para ele.
- **v1.3 publicada (2026-10-21).** Próxima: **12** desempenho (precisa da placa).
- **Etapa 12 (nova, última, pedido do usuário)**: investigar e melhorar desempenho do Spotify (ver "Pendências de desempenho").
- **Etapa 12, itens 1 e 2 concluídos (2026-10-22)**: LVGL e lista da playlist na PSRAM (+27 KB de RAM interna no
  Músicas), token renovado em segundo plano. Item 3 (jogo ~30 FPS) pendente; item 4 removido pelo usuário.
- **App Ler (pedido do usuário, 2026-10-22)**: palavra + 3 imagens, 3 níveis, sons de acerto/erro. Spec em
  `docs/specs/apps/ler.md`. Gravado na placa; conferência visual/sonora do usuário pendente.
- **Wi-Fi com até 5 redes (pedido do usuário)**: casa e casa da sogra sem recadastrar. Compilado; ainda
  não gravado nem testado na placa.
- **Specs (2026-10-24)**: `docs/specs/` com o núcleo, cada app e a proposta *Monte a Palavra* (não implementada,
  decisões D1–D9 pendentes; trazida de `erickmobltda/my_readmes`).
- **v1.4 publicada (2026-10-24)** a pedido do usuário, com Ler e Wi-Fi ainda sem teste na placa.

## Estrutura do sketch `sketches/painel`
| Arquivo | Papel |
|---|---|
| `painel.ino` | setup/loop: Wire → display → power → home; loop chama `lv_timer_handler()` |
| `board.h` | pinos e rotação (`LCD_ROTATION 1` = paisagem 480x320) |
| `display.*` | reset LCD/touch via TCA9554, Arduino_GFX ST7796, touch FT6X36 (com conversão de coordenadas p/ rotação), LVGL 8 |
| `power.*` | AXP2101 **somente leitura** (liga medições ADC; não altera tensões nem carga) |
| `ui.*` | telas (home, wifi_setup, settings, placeholders); `loadScreen()` troca tela, apaga a anterior com `lv_obj_del_async`, loga heap/PSRAM e roda o `refresh` da tela a cada 2 s |
| `config.*` | NVS (Preferences, namespace `painel`): Wi-Fi + Spotify; fallback para `secrets.h` (fora do git; modelo em `secrets.example.h`) |
| `net.*` | Wi-Fi (com nova tentativa a cada 15 s) + WebServer porta 80 + DNSServer (portal cativo) + mDNS `painel.local`; tudo do core |
| `clock.*` | NTP (`pool.ntp.org`), fuso Brasília `<-03>3`; data de hoje e validade do token (autorização + 6 meses) |
| `spotify.*` | tarefa FreeRTOS que consulta `GET /v1/me/player` a cada 1 s só com a tela aberta; renova access token; troca code→refresh token |
| `leitura.*`, `leitura_assets.h`, `font_le_64.c` | app Ler: regras/NVS, palavras+imagens geradas por `tools/leitura_assets.py`, fonte 64 px |
| `fonts.h`, `font_pt_{16,20,28}.c` | Montserrat com acentos (Latin-1) + símbolos LVGL, gerada com `npx lv_font_conv@1.5.2` (comando no cabeçalho de cada .c) |

## Spotify (decisões e armadilhas)
- Autorização (decisão do usuário: os dois caminhos):
  1. Página da placa: PIN + Client ID/Secret → "Abrir login" (JS monta a URL com `state` sorteado por boot) → usuário cola
     o endereço da página de erro `http://127.0.0.1:8888/callback?code=...` → `POST /spotify/code` → a placa troca o code
     (`grant_type=authorization_code`) e salva com a data de hoje (NTP).
  2. `python3 tools/spotify_auth.py` (só biblioteca padrão): servidor em 127.0.0.1:8888, troca, testa `/v1/me/player`,
     envia para `POST /spotify` (pin, client_id, client_secret, refresh_token, auth_date). `--print` gera linhas p/ `secrets.h`.
- HTTPS validado com o CA bundle embutido no core (`_binary_x509_crt_bundle_start` em libmbedtls.a) via `setCACertBundle`.
  Sem `setInsecure()`. Obs.: `CONFIG_MBEDTLS_HAVE_TIME_DATE` desligado no core → datas de validade dos certificados não são checadas.
- Tarefa `spotify`: **núcleo 1, prioridade 0** (ver LESSONS: watchdog). Pilha 8 KB, mínimo livre medido ~3,3 KB.
  Conexão TLS mantida aberta (keep-alive) enquanto a tela está aberta; fechada ao sair (libera ~45 KB).
- Consulta: `?additional_types=track,episode&market=from_token` (~3 KB), ArduinoJson com filtro. 204/`item` nulo →
  "Nenhum dispositivo tocando"; 401 → renova token; 429 → espera `Retry-After`; `invalid_grant` → "Reautorize" (para de tentar).
- Depuração: enviar `s` pelo serial liga/desliga a consulta sem abrir a tela (usado para testar sem tocar na placa).
- Comandos (`spotifyCommand`): fila FreeRTOS → a tarefa executa (PUT play/pause/seek, POST next/previous) e consulta
  logo depois; a tela atualiza o estado local na hora (ícone, posição). Cabeçalho `Content-Length: 0` adicionado à mão
  (o HTTPClient não manda com corpo vazio). 403/404/429 viram aviso laranja por 3 s.
- Fila: `GET /v1/me/player/queue` (~53 KB para 20 itens, String na PSRAM) com filtro (nome, artistas, show); a consulta
  continua ativa ao ir Spotify ↔ Fila (`onLeave = nullptr` na navegação entre as duas). Depuração: `q` pelo serial busca a fila.
- Capa: menor imagem ≥150 px (normalmente a de 300) baixada de `i.scdn.co` numa **segunda conexão TLS mantida aberta**,
  decodificada com o TJpgDec do LVGL (`LV_USE_SJPG 1`, escala 1/2 → 150x150, RGB888→RGB565) para a PSRAM; a tela recebe
  o buffer por `spotifyTakeArt` (contador de versão) e libera ao trocar/fechar. Falha no download → nova tentativa em 10 s.
- Tarefa `spotify`: pilha 12 KB (mínimo livre ~6,7 KB), núcleo 1, prioridade 0.

## Jogo (Capitã Maeva) — `game.cpp`, `game_atlas.h`
- Fonte: `/Users/erickmob/code/gh/personalRepos/capita_maeva_game/index.html` (resumo aprovado pelo usuário em 2026-10-20).
  Port fiel: mesmas constantes (GRAV 1100, JUMP_H 70, PREP/LAND/INV, velocidade 150→230), mesma ordem de funções e nomes.
- Sprites da Maeva: os 2 atlas PNG embutidos no HTML viram `game_atlas.h` (RGB565, cor-chave 0xF81F, ~420 KB na flash) com
  `python3 tools/game_assets.py <index.html>` (regerar se o jogo original mudar). O resto (céu, nuvens, morros, chão,
  obstáculos, abelha, corações, botões, fonte 5x7) é gerado em C++ na entrada do jogo (PSRAM), como no JS.
- Tela da placa 480x320 vs jogo 480x270: mundo idêntico (1:1); as 50 linhas extras são mais terra, onde ficam os botões.
- Decisões do usuário: só os dois botões (Pular vermelho à esq., Abaixar azul à dir., 96 px, alvo com +10 px de tolerância)
  valem durante o jogo; no título/game over, tocar em qualquer lugar começa (como o canvas do original). Casinha no topo
  (lugar do botão de mudo) volta ao Painel, ao soltar o dedo. Touch lê 2 dedos (segurar Abaixar + tocar Pular funciona).
- Recorde na NVS (`game_best`). Wi-Fi desligado durante o jogo (`netPause/netResume`). `gameStop()` libera tudo.
- Desenho: 10 faixas de 480x32 num buffer na RAM interna (30 KB); `render()` é chamado por faixa e as primitivas recortam.
  Nuvens/morros como "trechos opacos" (memcpy); sprites ampliados uma vez; `#pragma GCC optimize("O2")` no game.cpp.
- Tela: barramento **Arduino_ESP32SPIDMA a 80 MHz, sem MISO** (o driver do ESP-IDF limita a 26,7 MHz com MISO); o jogo
  inverte os bytes da faixa e envia com `draw16bitBeRGBBitmap` (DMA direto do buffer). Envio 32 ms + desenho ~15 ms = ~20 FPS.
  ⚠️ O SD (CS 15) divide SCK/MOSI/MISO com a tela e o barramento da tela está sem MISO: hoje nada usa o SD.
  Para usar o SD (ex.: cache de capas da etapa 12) será preciso planejar o compartilhamento do barramento.
- Usuário: as animações da tela de título do jogo podem ser reduzidas se precisar de desempenho.

## App Músicas (decisões do usuário, v1.1) — `ui.cpp` (tela) + `spotify.cpp` (dados)
- Tela inicial: **Músicas | Jogo / Pintar | Ler** (grade 2x2 desde a v1.4). A tela "Spotify" não tem mais botão (código em `spotifyEvent`, mantido).
- Grade de 8 capas (4x2, 112 px) da playlist do usuário (`5BLKGFcPOFthBIkYROwHLK`, 37 músicas/28 álbuns em 2026-10-20);
  barra ⌂ ◀ ⏯ ▶. Tocar numa capa = `PUT /me/player/play` com `context_uri` da playlist + `offset.uri` da música
  (toca e segue a playlist). Sem aparelho ativo (404) → `GET /devices` → prefere tipo "Speaker" (Alexa), senão o 1º;
  lista vazia → aviso amarelo. Moldura verde na capa que está tocando (`trackUri` do player).
- Regra do Spotify (fev/2026, doc oficial): `GET /playlists/{id}/items` só devolve músicas de playlists **da conta
  autorizada**; o endpoint antigo `/tracks` foi removido. Cada token só enxerga os aparelhos da própria conta.
- Playlist configurável na página da placa (form "Playlist do app Músicas", aceita link/URI/ID) → NVS `kid_playlist`.
- Capas: baixadas da `i.scdn.co` (300 px) → TJpgDec 1/2 → reduzidas a 112x112 RGB565 → LittleFS `/capas/<albumId>.565`
  (~28 KB cada com os blocos da LittleFS). Lista em cache `/musicas.txt` (abre na hora; atualiza pela internet).
- **Uma conexão TLS por vez no modo Músicas** (duas não cabem na RAM): enquanto faltam capas, a API fica fechada e o
  player não é consultado; pedido de tocar/comando tem prioridade.
- Espaço: desde a v1.3 `sketches/painel/partitions.csv` (huge_app com LittleFS de 896 KB → 12,9 MB, coredump em
  0xFF0000). NVS no mesmo lugar e a LittleFS **cresceu sem formatar** (esp_littlefs aceitou: 828/13184 KB, capas e
  desenho mantidos). `saveThumb` ainda para quando sobra < 64 KB.

## App Pintar — `paint.cpp` (decisões do usuário)
- Tela inicial com 3 botões lado a lado (Spotify | Jogo | Pintar, 140x250).
- Área de desenho 384x320 (canvas RGB565 na PSRAM) + barra de 96 px à direita: casinha (voltar ao soltar), lixeira
  ("Apagar tudo?" Sim/Voltar), 8 cores, glitter (pontinhos de 6 cores brilhantes, parados), borracha (2x a espessura),
  3 espessuras (raios 3/7/14). Traço contínuo: carimbos interpolados entre leituras do touch.
- Desenha direto na tela (sem LVGL): só o retângulo alterado é enviado (bytes invertidos na RAM interna + DMA).
- Salva em LittleFS (partição `spiffs` de 12,9 MB, `partitions.csv`), `/desenho.bin` em RLE (pares quantidade+cor):
  ao sair e 15 s depois do último traço. Na 1ª vez a partição é formatada (o log mostra "mount failed" uma vez: normal).
- Abre via `paintRequestStart()` → `paintStartIfRequested()` no loop, DEPOIS do `lv_timer_handler()` (ver LESSONS).
- Depuração: `p` pelo serial abre o Pintar; `m` abre o Músicas.

## Energia — `idle.cpp`
- Brilho por PWM (`ledcAttach` no pino 25): aceso 255, escurecido 40, apagado 0. USB detectado pelo AXP2101 (a cada 1 s).
- Atividade = qualquer toque (`displayLastTouch`, inclusive no jogo) ou partida em andamento (`gamePlaying()`).
- Tela apagada: o `loop()` não desenha (nem LVGL nem jogo), só `netLoop()`; consulta ao Spotify suspensa
  (`spotifySetSuspended`); música do jogo para. O toque que acorda é engolido até soltar (`displaySetSleeping`).
- Botões da placa (etiqueta): **PWR · RST · BOOT** (esq. → dir.). RST reinicia (hardware). PWR = GPIO 36, nível alto
  quando apertado: **toque rápido (< 1 s) apaga/acende a tela** (decisão do usuário; também acende tocando na tela);
  pressão longa continua com o AXP2101 (desligar) — o firmware não configura esse tempo.
- **Caixa-preta** `/diag.txt` (LittleFS, até 8 KB): boot + motivo do reinício, mudanças de tela com o motivo (toque com
  coordenada, USB, jogo, botão PWR) e mudanças USB/bateria. Serial: `i` mostra, `I` apaga.
- Caixa-preta de 2026-10-20/21 (bateria real): escureceu e apagou certo; bateria ~10%/h com tela apagada (CPU 240 MHz +
  Wi-Fi + periféricos ligados) → zerou à noite. Houve 1 travamento ao acordar por toque: coredump (partição `coredump`
  0x3F0000 até a v1.2, 0xFF0000 desde a v1.3, ELF) → `abort()` em `lock_init_generic` (newlib locks.c:77) na `tcpip_thread` = sem RAM interna para criar
  um lock. Leitura: `esptool read-flash 0xFF0000 0x10000 coredump.bin` + addr2line (ver LESSONS).
- **Desligamento automático (decisão do usuário)**: na bateria, parada 10 min → `pmu.shutdown()` (AXP2101). Testado
  com bateria simulada: desligou aos 600 s (até a porta USB sumiu). Religar = toque no PWR — **não confirmado ainda**.
  Toque na tela não acorda a placa desligada. A caixa-preta agora grava a RAM interna livre/maior bloco em cada evento.
- Depuração: `b` pelo serial simula bateria com o USB ligado.
- Depuração: `g` pelo serial abre o jogo.

## Som — `sound.cpp`, `es8311.*` (driver oficial copiado do exemplo 04_es8311_example)
- Decisão do usuário (2026-10-20): **sintetizar na placa** em vez de WAVs no SD (desvio do briefing; não existe `sd_card/`).
  Port do `createSound()` do HTML: mesmas notas (TITLE 132 BPM, GAME 164 BPM), ondas, rampas (Param = setValueAtTime/
  linear/exponential), ruído com passa-altas (Q 1 dB), kick/snare, barramentos (música 0,8 com fade 0,03 s/300 ms).
- Codec ES8311 via `Wire` (I2C 0x18), 48 kHz, RESOLUTION_32 (mesma config do exemplo oficial); I2S do core (`ESP_I2S`):
  BCLK 2, LRCK 4, DOUT 12; amplificador = TCA9554 pino 2 (`displaySetAmplifier`).
- Tarefa "som" no **núcleo 0** (prio 3): recebe pedidos numa fila, agenda as vozes (32) e mistura blocos de 256 amostras.
  O jogo só chama `soundPlay(Snd::Jump)` etc. — nunca bloqueia.
- `soundEnable(true/false)`: I2S + tarefa + amplificador só no jogo e na ⚙ (~12 KB de RAM interna e bateria).
- Volume (0–100, padrão 80) e mudo do jogo na NVS (`volume`, `game_mute`). Slider na ⚙ toca bipe ao soltar.

## Pendências de desempenho (Etapa 12)
Medições de 2026-10-20 (Wi-Fi −65 dBm; do Mac a API responde em 0,18 s e a capa baixa em 0,07 s):
- Abrir TLS com `i.scdn.co` leva 3–10 s (primeira capa ao abrir a tela); capas seguintes ~0,8 s de download + 0,2–1,1 s de decodificação.
- Primeira consulta após (re)conectar à API: 1,5–3 s; consultas com conexão aberta: ~0,3 s.
- Enquanto a capa baixa/decodifica, a tarefa não consulta nem executa comandos (botões esperam).
- Com as duas conexões TLS abertas: heap interno livre **~53 KB, maior bloco ~30 KB** — pouca folga.
- Já testado sem efeito: prioridade da tarefa 0 vs 1; desligar o modem sleep do Wi-Fi (mantido desligado só com a tela aberta).
- **Pedido do usuário: avaliar cache das capas no cartão SD** (a filha ouve sempre as mesmas músicas): salvar o JPEG
  (ou o RGB565 150x150 já decodificado, 45 KB) com nome = id da imagem na URL; na repetição evita o TLS com `i.scdn.co`
  (3–10 s) e a decodificação. Atenção: SD divide o barramento SPI com o LCD (CS 15) — acessar sob o mesmo controle do LVGL.
- Jogo: ~20 FPS limitado pelo envio (32 ms/quadro a 80 MHz). Ideia: montar a próxima faixa enquanto o DMA envia a anterior
  (2 buffers; precisa de envio assíncrono, a GFX só tem POLL_START+POLL_END juntos) → até ~30 FPS. Game over a ~15 FPS
  (escurece a tela toda a cada quadro; dá para escurecer só uma vez). Título pode ter animações reduzidas (ok do usuário).
- Ideias: tarefa separada para a capa; decodificar com `JD_FASTDECODE`; investigar por que o handshake demora (CPU vs rede,
  `CONFIG_MBEDTLS_*`, sessão TLS reaproveitada); reduzir buffers TLS; HTTP/1.0 vs keep-alive.

## Wi-Fi e página de configuração (decisões do usuário)
- Até 5 redes na NVS (`wifi_ssid`/`wifi_pass` = a mais recente, `wifi_ssid1..4`/`wifi_pass1..4`). Com mais de uma,
  busca assíncrona (`WiFi.scanNetworks(true)`) e conecta na salva de sinal mais forte; busca falhou → a mais recente.
  Salvar põe na frente, troca a senha se já existia e esquece a mais antiga acima de 5.
- Sem Wi-Fi salvo → ponto de acesso `Painel-XXXX` (XXXX = 2 últimos bytes do MAC, aqui `Painel-BCFC`), senha de 8 dígitos
  sorteada a cada ativação e mostrada na tela; portal cativo em `http://192.168.4.1`. Salvar → reinicia e conecta (as outras redes salvas continuam).
- Conectado → `http://<ip>/` ou `http://painel.local/`: formulário do Spotify (`POST /spotify` com `pin, client_id,
  client_secret, refresh_token, auth_date` AAAA-MM-DD; resposta `200 OK`). PIN de 6 dígitos novo a cada boot, na tela ⚙.
  5 PINs errados → bloqueio de 60 s (HTTP 429).
- ⚙ na home → Configurações (rede, IP, PIN, status Spotify) e "Trocar Wi-Fi" (sobe o AP; a rede antiga só é
  substituída ao salvar; "Cancelar" reinicia).
- Roteador fora do ar: status "sem conexao", o core reconecta sozinho (`setAutoReconnect`).
- Textos da tela com acento via `font_pt_*` (as fontes embutidas do LVGL só têm ASCII). "⚙" na tela = `LV_SYMBOL_SETTINGS`.

## Orientação e UI
- **Paisagem** (placa deitada), escolha do usuário. Rotação de hardware GFX = 1; touch convertido em `display.cpp`
  (`x = y_nativo`, `y = 319 - x_nativo`). Se um dia precisar girar 180°, usar `LCD_ROTATION 3` (mapeamento já existe).
- Fonte grande: `LV_FONT_MONTSERRAT_28` habilitada em `arduino/libraries/lv_conf.h` (este arquivo é versionado;
  o LVGL usa o `lv_conf.h` ao lado da pasta `lvgl`. O sketch `painel` NÃO tem lv_conf.h próprio, para não haver duas configs).

## Memória (medida na home)
- LVGL com **1 buffer de 480x40** (38 KB, RAM interna) — decisão do usuário; o flush é bloqueante, então 2 buffers não ajudavam.
- Com Wi-Fi conectado, na home: ~155 KB livres (maior bloco ~110 KB). Na tela do Spotify com as 2 conexões TLS: ~53 KB (maior ~30 KB).
- PSRAM livre ~2 MB (Strings > 4 KB vão para a PSRAM: `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`).
- Programa: 2,23 MB de 3 MB (71%) — o atlas do jogo ocupa ~420 KB.
- No jogo (Wi-Fi desligado, som ligado): heap interno ~115 KB livres (maior bloco ~63 KB), PSRAM ~1,83 MB.
- Home: ~135 KB livres (o barramento DMA da tela custa ~6 KB).
- Bateria: % do AXP2101 parece baixa para a tensão (14% @ 3,82 V); o chip calibra após um ciclo completo (comentário do exemplo oficial).

## Repositório e versões
- GitHub (público, decisão do usuário após varredura sem segredos): https://github.com/erickmobltda/painel-esp32 — branch `main`.
- **Release v1.2** (2026-10-20, tag `v1.2`): melhorias de toque/visual (mesmo esquema de anexos, `painel-v1.2-*`).
- **Release v1.1** (2026-10-20, tag `v1.1`): app Músicas. Mesmo esquema de anexos/comando da v1.0 (arquivos `painel-v1.1-*`).
- **Release v1.0** (2026-10-20, tag `v1.0`): ponto de retorno seguro antes das melhorias. Anexos: bootloader, partitions,
  boot_app0 e app (.bin) + SHA256SUMS. Voltar sem apagar configuração/desenho:
  `esptool --chip esp32 --port $PORT --baud 921600 write-flash 0x1000 painel-v1.0-bootloader.bin 0x8000 painel-v1.0-partitions.bin 0xe000 painel-v1.0-boot_app0.bin 0x10000 painel-v1.0-app.bin`
  (ou `git checkout v1.0`, compilar e gravar). Não usar o `.merged.bin` de 16 MB: ele apaga NVS e LittleFS.
- Antes de publicar: conferir que nenhum `secrets.h`, Client ID/Secret, token ou senha de Wi-Fi entrou no histórico.

## Placa (confirmado com `esptool flash-id`)
- Chip ESP32-D0WDR2-V3 rev v3.1, flash 16 MB (Winbond), MAC 20:e7:c8:d2:bc:fc
- Porta serial no Mac: `/dev/cu.usbmodem5C371857381`

## Ferramentas e versões (fixadas)
| Item | Versão / local |
|---|---|
| arduino-cli | 1.5.1, o que vem no Arduino IDE: `/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli` |
| Core esp32:esp32 | **3.3.5**, isolado em `arduino-data/` (fora do git). O IDE continua com 3.3.12 em `~/Library/Arduino15` |
| esptool | 5.1.0 (vem com o core 3.3.5: `arduino-data/packages/esp32/tools/esptool_py/5.1.0/esptool`) |
| Repo oficial Waveshare | `vendor/ESP32-Touch-LCD-3.5`, commit `19f266e8c257f3ed5519e2e4cc062d9e5a64fde4` (fora do git) |
| Libs (`arduino/libraries`, fora do git, copiadas do repo oficial) | lvgl 8.4.0, GFX_Library_for_Arduino 1.6.0, SensorLib 0.3.1, TCA9554 0.1.2, XPowersLib **0.3.0**, OneButton 2.6.1, ESP32-audioI2S 3.4.0 |
| ArduinoJson | **7.4.3** (instalada com arduino-cli em `arduino/libraries`, aprovada pelo usuário) |

`arduino-cli.yaml` aponta `data` para `arduino-data/` e `user` para `arduino/` → nada de `~/Documents/Arduino` é usado.

### Decisões
- XPowersLib 0.3.0 (a que está no repo oficial; o site da Waveshare cita 0.2.9). Decisão do usuário.
- Core 3.3.5 em vez de 3.3.12: a GFX 1.6.0 não compila com core ≥ 3.3.6 (ver LESSONS.md). Isolado por decisão do usuário.
- Sem backup da flash original: restauração via `Firmware/01_factory.bin` oficial. Decisão do usuário.
- `arduino/libraries/` (~210 MB) não vai para o git; é recriada a partir do vendor (abaixo).

## FQBN
```
esp32:esp32:esp32:PSRAM=enabled,FlashSize=16M,PartitionScheme=huge_app
```
(ESP32 Dev Module, PSRAM ligado, 16 MB, Huge APP = 3 MB app sem OTA; o `partitions.csv` do sketch substitui a tabela)

## Como retomar do zero (máquina nova)
```bash
cd esp32
git clone https://github.com/waveshareteam/ESP32-Touch-LCD-3.5 vendor/ESP32-Touch-LCD-3.5
git -C vendor/ESP32-Touch-LCD-3.5 checkout 19f266e8c257f3ed5519e2e4cc062d9e5a64fde4
mkdir -p arduino/libraries
for l in ESP32-audioI2S-master GFX_Library_for_Arduino lvgl OneButton SensorLib TCA9554 XPowersLib; do
  cp -R vendor/ESP32-Touch-LCD-3.5/examples/Arduino/libraries/$l arduino/libraries/; done
"/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli" --config-file arduino-cli.yaml lib install ArduinoJson@7.4.3
# arduino/libraries/lv_conf.h já vem do git
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
"$CLI" --config-file arduino-cli.yaml core update-index
"$CLI" --config-file arduino-cli.yaml core install esp32:esp32@3.3.5
```

## Comandos do dia a dia
```bash
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
FQBN="esp32:esp32:esp32:PSRAM=enabled,FlashSize=16M,PartitionScheme=huge_app"
PORT=/dev/cu.usbmodem5C371857381
"$CLI" --config-file arduino-cli.yaml compile --fqbn $FQBN --build-path build/painel sketches/painel
"$CLI" --config-file arduino-cli.yaml upload -p $PORT --fqbn $FQBN --input-dir build/painel sketches/painel
"$CLI" --config-file arduino-cli.yaml monitor -p $PORT -c baudrate=115200
```
Se o upload travar em "Connecting...": segurar o botão BOOT da placa.

## Restaurar o firmware de fábrica
```bash
ET=arduino-data/packages/esp32/tools/esptool_py/5.1.0/esptool
"$ET" --chip esp32 --port $PORT write-flash 0x0 vendor/ESP32-Touch-LCD-3.5/Firmware/01_factory.bin
```

## Pinos confirmados (no exemplo 09)
LCD ST7796 SPI: DC 27, CS 5, SCK 18, MOSI 23, MISO 19; backlight 25; TCA9554 em 0x20 (reset LCD/touch).
Touch FT6336 via `TouchDrvFT6X36` (SensorLib). Demais pinos (I2S, SD, AXP2101) serão conferidos nas etapas que os usam.
