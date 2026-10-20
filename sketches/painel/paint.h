#pragma once

// App "Pintar": tela branca, 8 cores, glitter, borracha, 3 espessuras, apagar tudo (com confirmação).
// O desenho fica salvo na flash (LittleFS) e volta ao abrir de novo. Desenha direto na tela, sem LVGL,
// como o jogo: enquanto roda, o loop() chama paintFrame() em vez de lv_timer_handler().

void paintStart();
// Pedido feito dentro de um evento do LVGL: abre só depois que o LVGL terminar de desenhar
// (senão ele redesenha o botão "solto" por cima da tela do Pintar).
void paintRequestStart();
void paintStartIfRequested();
bool paintRunning();
// Devolve false quando o usuário tocou em "voltar" (aí: paintStop() e tela inicial).
bool paintFrame();
// Salva o desenho e libera a memória.
void paintStop();
