#pragma once

// Capitã Maeva: port do index.html (github erickmob/capita_maeva_game) desenhando direto na tela,
// sem LVGL. Enquanto o jogo roda, o loop() chama gameFrame() em vez de lv_timer_handler().

// Aloca o jogo, desliga o Wi-Fi e assume a tela.
void gameStart();
bool gameRunning();
// Um quadro (lógica + desenho). Devolve false quando o usuário tocou em "voltar":
// aí o chamador faz gameStop() e mostra a tela inicial.
bool gameFrame();
// Libera toda a memória do jogo e religa o Wi-Fi.
void gameStop();
