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
- Próxima: **Etapa 8** — sons do jogo (decidir com o usuário: só efeitos ou também música).
- **Etapa 12 (nova, última, pedido do usuário)**: investigar e melhorar desempenho do Spotify (ver "Pendências de desempenho").

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
  ⚠️ Etapa 8: o SD (CS 15) divide SCK/MOSI/MISO com a tela; o barramento da tela está sem MISO → planejar o acesso ao SD.
- Usuário: as animações da tela de título do jogo podem ser reduzidas se precisar de desempenho.
- Depuração: `g` pelo serial abre o jogo.

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
- Sem Wi-Fi salvo → ponto de acesso `Painel-XXXX` (XXXX = 2 últimos bytes do MAC, aqui `Painel-BCFC`), senha de 8 dígitos
  sorteada a cada ativação e mostrada na tela; portal cativo em `http://192.168.4.1`. Salvar → reinicia e conecta.
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
- Programa: 2,14 MB de 3 MB (68%) — o atlas do jogo ocupa ~420 KB.
- No jogo (Wi-Fi desligado): heap interno ~136 KB livres (maior bloco ~74 KB), PSRAM ~1,83 MB.
- Bateria: % do AXP2101 parece baixa para a tensão (14% @ 3,82 V); o chip calibra após um ciclo completo (comentário do exemplo oficial).

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
(ESP32 Dev Module, PSRAM ligado, 16 MB, Huge APP = 3 MB app sem OTA / 1 MB SPIFFS)

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
