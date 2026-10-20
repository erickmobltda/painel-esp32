# Lessons

## [2026-10-20] — GFX 1.6.0 não compila com core esp32 ≥ 3.3.6
- **What happened**: `spiFrequencyToClockDiv` "too few arguments" em `Arduino_ESP32SPI*.cpp` com core 3.3.12.
- **Why**: no core 3.3.6 a Espressif mudou a assinatura para `spiFrequencyToClockDiv(spi_t*, uint32_t)` (conferido nos headers das tags 3.3.5 e 3.3.6). A Waveshare não fixa a versão do core.
- **Rule**: usar o core 3.3.5 isolado em `arduino-data/`; não atualizar o core sem antes checar compatibilidade com as libs da Waveshare.

## [2026-10-20] — Confirmar o dispositivo antes de gravar
- **What happened**: o usuário perguntou como garantir que a gravação não atinge o HD/outro dispositivo.
- **Rule**: antes de gravar numa porta nova, rodar `esptool --port <p> flash-id` (somente leitura), mostrar chip/flash/MAC e esperar o ok.
