# PROJECT_CONTEXT — Painel (Waveshare ESP32-Touch-LCD-3.5)

Objetivo, regras e etapas completas: ver o briefing original (Spotify remote + Jogo do Pulo).

## Estado atual
- **Etapa 1 concluída (2026-10-20)**: `09_lvgl_arduino_v8` oficial, sem modificação, compilado e gravado;
  usuário confirmou na placa texto + switches + toque.
- **Etapa 2 concluída (2026-10-20)**: sketch `sketches/painel` com tela inicial em paisagem (Spotify | Jogo),
  barra de status (Wi-Fi placeholder, bateria %/carregando/USB). Usuário confirmou na placa: orientação, botões, voltar, ⚡ some sem USB.
- Próxima: **Etapa 3** — Wi-Fi: AP de configuração + conexão + página de config com PIN.

## Estrutura do sketch `sketches/painel`
| Arquivo | Papel |
|---|---|
| `painel.ino` | setup/loop: Wire → display → power → home; loop chama `lv_timer_handler()` |
| `board.h` | pinos e rotação (`LCD_ROTATION 1` = paisagem 480x320) |
| `display.*` | reset LCD/touch via TCA9554, Arduino_GFX ST7796, touch FT6X36 (com conversão de coordenadas p/ rotação), LVGL 8 |
| `power.*` | AXP2101 **somente leitura** (liga medições ADC; não altera tensões nem carga) |
| `ui.*` | telas; `loadScreen()` troca tela, apaga a anterior com `lv_obj_del_async` e loga heap/PSRAM |

## Orientação e UI
- **Paisagem** (placa deitada), escolha do usuário. Rotação de hardware GFX = 1; touch convertido em `display.cpp`
  (`x = y_nativo`, `y = 319 - x_nativo`). Se um dia precisar girar 180°, usar `LCD_ROTATION 3` (mapeamento já existe).
- Fonte grande: `LV_FONT_MONTSERRAT_28` habilitada em `arduino/libraries/lv_conf.h` (este arquivo é versionado;
  o LVGL usa o `lv_conf.h` ao lado da pasta `lvgl`. O sketch `painel` NÃO tem lv_conf.h próprio, para não haver duas configs).

## Memória (medida na home)
- Heap interno livre ~162 KB (maior bloco ~59 KB), PSRAM livre ~2 MB. Os 2 buffers LVGL (480x80x2 = 77 KB cada) estão
  na RAM interna — candidato a reduzir/mover se TLS apertar nas etapas 3/4.
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
| ArduinoJson | ainda não instalada (definir versão na etapa 4) |

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
