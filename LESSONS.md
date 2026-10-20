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

## [2026-10-20] — WiFi.macAddress() antes de iniciar o Wi-Fi
- **What happened**: o nome do AP saiu `Painel-FB3F`, sem relação com o MAC real (…bc:fc).
- **Rule**: para identificar a placa antes do Wi-Fi subir, usar `ESP.getEfuseMac()`.

## [2026-10-20] — Captura serial em background
- **What happened**: capturei 10 min com saída só no final; ao encerrar o processo, o log se perdeu.
- **Rule**: o script de captura deve escrever na saída em tempo real (flush) e não depender de terminar.

## [2026-10-20] — Unidade do timeout no NetworkClientSecure (core 3.x)
- **What happened**: `client.setTimeout(8)` achando que era segundos → 8 ms → toda conexão falhava com HTTP -1.
- **Rule**: no core 3.x `setTimeout` é em **milissegundos**; deixar o `HTTPClient::setTimeout(ms)` cuidar disso.

## [2026-10-20] — `NetworkClientSecure::stop()` apaga o vínculo com o CA bundle
- **What happened**: depois do primeiro `stop()`, toda conexão falhava (HTTP -1). `stop_ssl_socket` faz `memset` do contexto, zerando `bundle_attach_cb`.
- **Rule**: chamar `setCACertBundle(...)` antes de cada conexão nova num cliente reaproveitado (`if (!client.connected())`).

## [2026-10-20] — Watchdog com HTTPClient em tarefa no núcleo 0
- **What happened**: a placa reiniciava (task_wdt, IDLE0) durante `getString()`: o HTTPClient espera dados girando com `delay(0)`, que não cede a CPU para a IDLE.
- **Rule**: tarefas de rede com HTTPClient ficam no **núcleo 1, prioridade 0** (watchdog não vigia IDLE1; loop/LVGL tem prioridade). Decodificar backtrace com `xtensa-esp32-elf-addr2line -pfiaC -e build/painel/painel.ino.elf <endereços>`.

## [2026-10-20] — Logs: filtrar com grep esconde a causa
- **What happened**: o filtro `grep "^\[|Guru|panic"` escondeu as linhas `E (...) task_wdt` do travamento.
- **Rule**: em testes de estabilidade, capturar o serial sem filtro (só remover ruído conhecido) e usar `grep --line-buffered`.

## [2026-10-20] — Testar sozinho antes de pedir teste ao usuário
- **What happened**: pedi ao usuário 3 rodadas de teste para bugs que eu podia reproduzir.
- **Rule**: usar o comando de depuração `s` pelo serial (e o script de captura com envio `T:c`) para validar a lógica antes; pedir ao usuário só o que exige o toque/olho humano.

## [2026-10-20] — Leitura de corpo grande com NetworkClientSecure
- **What happened**: a capa chegava pela metade: `read()`/`readBytes()` devolvem -1/parcial assim que não há dados no momento.
- **Rule**: ler em laço com `available()` + `delay()` até completar o `Content-Length` ou estourar um prazo.

## [2026-10-20] — Captura serial segura a porta
- **What happened**: o upload falhou ("No serial data received") porque a captura em background ainda usava a porta; testei o firmware antigo achando que era o novo.
- **Rule**: sempre `pkill -f scratchpad/serial.py` antes de gravar, e conferir "Wrote ... at 0x00010000" antes de testar.

## [2026-10-20] — Ler o log inteiro antes de concluir
- **What happened**: concluí que "tudo ficou lento" olhando um log que só registra consultas lentas.
- **Rule**: lembrar quais filtros o próprio log aplica antes de tirar conclusões; medir as fases separadamente (cabeçalho, corpo, decodificação).

## [2026-10-20] — Desempenho da tela SPI (ST7796) no ESP32
- **What happened**: o jogo começou a 7,6 FPS. Medindo por fase: montagem 44 ms + envio 84 ms.
- **Why**: `Arduino_ESP32SPI` envia 32 pixels por vez (FIFO de 64 bytes) e inverte bytes na CPU; o ESP32SPIDMA também inverte pixel a pixel em `draw16bitRGBBitmap`; código compilado com -Os; laços com divisão por pixel.
- **Rule**: medir cada fase antes de otimizar; para tela cheia usar `Arduino_ESP32SPIDMA` a 80 MHz sem MISO + buffer na RAM interna já em big-endian + `draw16bitBeRGBBitmap`; `#pragma GCC optimize("O2")` no arquivo quente.

## [2026-10-20] — Conferir tabelas copiadas à mão
- **What happened**: dois glifos da fonte 5x7 ficaram com um caractere a menos ao converter do JS.
- **Rule**: gerar tabelas a partir da fonte original por script (ou conferir por script), nunca redigitar.

## [2026-10-20] — Toques fora dos botões no título/game over
- **What happened**: no game over, tocar em "TENTAR DE NOVO" não fazia nada (só os botões valiam).
- **Rule**: ao restringir controles, manter o comportamento do original fora da jogabilidade (no original o canvas inteiro chama pressJump).
