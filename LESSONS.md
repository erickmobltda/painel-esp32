# Lessons

## [2026-10-20] — GFX 1.6.0 não compila com core esp32 ≥ 3.3.6
- **What happened**: `spiFrequencyToClockDiv` "too few arguments" em `Arduino_ESP32SPI*.cpp` com core 3.3.12.
- **Why**: no core 3.3.6 a Espressif mudou a assinatura para `spiFrequencyToClockDiv(spi_t*, uint32_t)` (conferido nos headers das tags 3.3.5 e 3.3.6). A Waveshare não fixa a versão do core.
- **Rule**: usar o core 3.3.5 isolado em `arduino-data/`; não atualizar o core sem antes checar compatibilidade com as libs da Waveshare.

## [2026-10-20] — Confirmar o dispositivo antes de gravar
- **What happened**: o usuário perguntou como garantir que a gravação não atinge o HD/outro dispositivo.
- **Rule**: antes de gravar numa porta nova, rodar `esptool --port <p> flash-id` (somente leitura), mostrar chip/flash/MAC e esperar o ok.

## [2026-10-20] — "Retrato/em pé" é ambíguo para o usuário
- **What happened**: construí em retrato (320x480); o usuário segura a placa deitada e esperava o desenho na posição de leitura.
- **Rule**: ao falar de orientação, descrever pela posição física ("placa mais larga que alta, conector de qual lado") e confirmar com foto antes de desenhar telas.

## [2026-10-20] — Trocar de tela dentro do evento de clique
- **What happened**: `lv_scr_load_anim(..., 0, 0, true)` no LVGL 8.4 apaga a tela antiga na hora, inclusive o botão cujo evento está rodando.
- **Rule**: trocar com `lv_scr_load(new)` + `lv_obj_del_async(old)` (função `loadScreen` em `ui.cpp`).
