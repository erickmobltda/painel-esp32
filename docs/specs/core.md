# Núcleo do Painel

## Placa e pinos
Waveshare ESP32-Touch-LCD-3.5 (ver [README](../../README.md#hardware)). Pinos usados, conferidos nos exemplos oficiais:

| Função | Pinos |
|---|---|
| Tela ST7796 (SPI) | DC 27, CS 5, SCK 18, MOSI 23 (MISO 19 não é usado pela tela) · backlight 25 (PWM) |
| I2C (toque FT6336, AXP2101, ES8311 0x18, TCA9554 0x20) | barramento `Wire` do core |
| TCA9554 | reset da tela, reset do toque, liga/desliga o amplificador (pino 2) |
| Áudio I2S | BCLK 2, LRCK 4, DOUT 12 |
| Botão PWR | GPIO 36 (alto = apertado) |

O slot microSD (CS 15) divide SCK/MOSI/MISO com a tela e **não é usado**.

## Inicialização e laço (`painel.ino`)
`Wire` → tela/toque/LVGL (`display.*`) → AXP2101 (`power.*`) → LittleFS → som → Wi-Fi e configuração (`net.*`) →
Spotify → tela inicial (ou a de cadastro do Wi-Fi, se não houver rede salva) → energia (`idle.*`). O `loop()` roda `lv_timer_handler()`, `netLoop()`, `idleLoop()` e os comandos do serial.

## Telas (`ui.*`)
- Paisagem 480×320. LVGL 8.4 com um buffer de 480×40 linhas na RAM interna; o resto da memória do LVGL na PSRAM.
- `loadScreen()` troca a tela, apaga a anterior e loga RAM/PSRAM livres. `onLeave` roda ao sair da tela.
- Tela inicial: grade 2×2 (**Músicas · Jogo / Pintar · Ler**), fundo rosa claro (#FADDE8). Barra de cima: Wi-Fi,
  ⚙ Configurações, bateria (%, ⚡ carregando, USB).
- ⚙ Configurações: rede, IP, PIN da página, validade do Spotify, volume (bipe ao soltar) e **Trocar Wi-Fi**.
- Fontes com acento: Montserrat 16/20/28 (Latin-1) e 64 (só maiúsculas, para o Ler), geradas com
  `npx lv_font_conv@1.5.2` (comando no cabeçalho de cada `font_*.c`).

## Energia (`idle.cpp`, `power.cpp`)
| Situação | Comportamento |
|---|---|
| No USB | tela sempre acesa |
| Na bateria, sem toque | escurece em 1 min (brilho 40/255), apaga em 5 min, desliga em 10 min (`pmu.shutdown()`) |
| Partida do jogo em andamento | não escurece |
| Toque com a tela escura/apagada | só acende; o toque é engolido até soltar o dedo |
| PWR rápido (< 1 s) | apaga/acende a tela |
| PWR segurado | desliga pelo AXP2101 (o firmware não muda esse tempo) |
| Placa desligada | liga com um toque rápido no PWR; o toque na tela não liga |

Tela apagada: nada é desenhado, a consulta ao Spotify é suspensa e a música do jogo para. O AXP2101 é usado só para
leitura (bateria, USB) e para o desligamento; o firmware não altera tensões nem carga.

## Wi-Fi (`net.cpp`, `config.cpp`)
- Até **5 redes** salvas. Ao ligar e a cada nova tentativa (15 s sem conexão): com uma rede, conecta direto; com mais,
  faz uma busca assíncrona e conecta na **salva de sinal mais forte**. Se a busca falhar, tenta a mais recente.
- Salvar uma rede a põe na frente da lista; se já existia, troca a senha; com mais de 5, esquece a mais antiga.
- Sem rede salva (ou ⚙ → Trocar Wi-Fi): ponto de acesso `Painel-XXXX` (fim do MAC) com senha de 8 dígitos sorteada e
  mostrada na tela, portal cativo em `http://192.168.4.1`. Salvar reinicia; "Cancelar" reinicia sem mudar nada.
- Durante o jogo o Wi-Fi é desligado (`netPause`/`netResume`).

## Página de configuração (`net.cpp`)
`http://<ip>/` ou `http://painel.local/` (mDNS). Toda gravação pede o **PIN de 6 dígitos** mostrado na ⚙, sorteado a
cada boot; 5 PINs errados bloqueiam por 60 s (HTTP 429).

| Rota | Uso |
|---|---|
| `GET /` | página com os formulários |
| `POST /wifi` | salva uma rede (modo ponto de acesso) |
| `POST /spotify` | Client ID, Client Secret, refresh token, data da autorização |
| `POST /spotify/code` | troca o `code` do login do Spotify por um refresh token |
| `POST /playlist` | playlist do app Músicas (link, URI ou ID) |

## Dados salvos
**NVS** (namespace `painel`; se vazia, usa os valores de `secrets.h`, que fica fora do git):

| Chave | Conteúdo |
|---|---|
| `wifi_ssid`, `wifi_pass`, `wifi_ssid1..4`, `wifi_pass1..4` | redes Wi-Fi (0 = a mais recente) |
| `sp_id`, `sp_secret`, `sp_refresh`, `sp_date` | Spotify |
| `kid_playlist` | playlist do Músicas |
| `volume`, `game_mute`, `game_best` | som e recorde do jogo |
| `le_level`, `le_stars` | progresso do Ler |

**Flash** (`sketches/painel/partitions.csv`): app 3 MB em 0x10000, LittleFS 12,9 MB em 0x310000, coredump em
0xFF0000. Arquivos na LittleFS: `/capas/<albumId>.565`, `/musicas.txt`, `/desenho.bin`, `/diag.txt`.
Gravar uma release pelos 4 `.bin` não apaga NVS nem LittleFS.

## Relógio (`clock.*`)
NTP (`pool.ntp.org`), fuso de Brasília. Usado para a data da autorização e a validade (6 meses) do Spotify, e para a
hora na caixa-preta.

## Som (`sound.*`, `es8311.*`)
- Sintetizador na placa (sem arquivos): tarefa no núcleo 0 com 32 vozes, 48 kHz, codec ES8311 + I2S.
- API: `soundEnable(bool)` liga I2S, tarefa e amplificador (~12 KB de RAM interna; só nas telas que usam som);
  `soundPlay(Snd::X)` nunca bloqueia. Volume 0–100 (padrão 80). O mudo vale só para o jogo; o Ler o ignora
  (`soundIgnoreMute`).
- Efeitos (`enum class Snd`): `Jump`, `Duck`, `Land`, `Hit`, `Start`, `GameOver`, `TitleMusic`, `StopMusic`, `Beep`
  e, do Ler, `Correct`, `Wrong`, `LevelUp`.

## Diagnóstico
- Serial 115200. Comandos de uma letra: `i`/`I` mostra/apaga a caixa-preta, `m` Músicas, `g` jogo, `p` Pintar,
  `l` Ler, `b` simula bateria, `s` liga/desliga a consulta do Spotify, `q` busca a fila do Spotify.
- **Caixa-preta** `/diag.txt` (até 8 KB): boot e motivo do reinício, mudanças de tela/energia com o motivo, USB/bateria,
  RAM livre em cada evento.
- **Coredump** em 0xFF0000 quando a placa trava (como ler: [LESSONS.md](../../LESSONS.md)).

## Memória (referência)
RAM interna livre: ~135 KB na home, ~125 KB no Músicas (~80 KB durante o TLS), ~115 KB no jogo. TLS (mbedTLS) sempre
usa RAM interna: **uma conexão HTTPS por vez** no Músicas. Mallocs > 4 KB vão para a PSRAM automaticamente.
