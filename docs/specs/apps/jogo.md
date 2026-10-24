# App Jogo — *Capitã Maeva*

Código: `game.cpp`, `game_atlas.h` (gerado), `sound.cpp`. Serial: `g` abre.

## Origem
Port fiel do jogo HTML `capita_maeva_game/index.html` (repositório pessoal do autor): mesmas constantes (gravidade 1100,
altura do pulo 70, velocidade 150→230), mesma ordem de funções e nomes. Os sprites da Maeva vêm dos 2 atlas PNG do HTML
(`python3 tools/game_assets.py <index.html>` → RGB565 com cor-chave 0xF81F, ~420 KB). Céu, nuvens, morros, chão,
obstáculos, abelha, corações, botões e a fonte 5×7 são gerados em C++ ao entrar (PSRAM).

## Como se joga
- Corrida infinita. Botões **Pular** (vermelho, esquerda) e **Abaixar** (azul, direita), 96 px com +10 px de tolerância;
  dois dedos funcionam (segurar Abaixar e tocar Pular).
- Título e game over: tocar em qualquer lugar começa. Casinha no topo volta ao Painel (ao soltar o dedo).
- Recorde salvo na NVS (`game_best`).
- Som sintetizado (música de título e de partida, efeitos), mudo pelo alto-falante ao lado da casinha (`game_mute`).

## Tela e desempenho
- Mundo 480×270 igual ao original; as 50 linhas extras da tela são terra, onde ficam os botões.
- Desenho em 10 faixas de 480×32 num buffer na RAM interna (30 KB), enviadas por DMA a 80 MHz (barramento sem MISO).
  ~20 FPS (envio 32 ms + desenho ~15 ms). Ideia registrada para ~30 FPS: montar a próxima faixa durante o DMA.
- Durante o jogo: Wi-Fi desligado, a tela não escurece numa partida. `gameStop()` libera tudo ao sair.
