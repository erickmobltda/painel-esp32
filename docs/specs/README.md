# Especificações do Painel

Como cada parte do firmware funciona **hoje** (o que foi implementado e testado), para quem for mudar ou criar um app
sem precisar ler todo o código. Decisões e histórico ficam em [`PROJECT_CONTEXT.md`](../../PROJECT_CONTEXT.md);
armadilhas em [`LESSONS.md`](../../LESSONS.md).

| Spec | O que cobre | Status |
|---|---|---|
| [core.md](core.md) | Placa, telas, energia, Wi-Fi, página de configuração, NVS, arquivos, som, diagnóstico | implementado |
| [apps/musicas.md](apps/musicas.md) | Capas de uma playlist do Spotify; tocar no celular ou na Alexa | implementado |
| [apps/jogo.md](apps/jogo.md) | *Capitã Maeva*, corrida infinita | implementado |
| [apps/pintar.md](apps/pintar.md) | Desenho livre com cores, glitter e borracha | implementado |
| [apps/ler.md](apps/ler.md) | Palavra + 3 imagens: tocar na imagem certa | implementado (v1.4) |
| [apps/monte-a-palavra.md](apps/monte-a-palavra.md) | Montar a palavra com sílabas | **proposta**, não implementada |

## Regras para um app novo
- Um botão na tela inicial (`uiShowHome` em `ui.cpp`) e uma função `uiShowX()` que cria a tela e chama `loadScreen`.
  Recursos grandes são liberados no `LV_EVENT_DELETE` da tela; o que precisa ser desfeito ao sair vai em `onLeave`.
- Tela 480×320 em paisagem; alvos de toque de pelo menos ~60 px para dedo de criança; uma casinha ⌂ volta à tela
  inicial (no canto superior esquerdo, salvo quando o app precisa da tela toda).
- RAM interna é curta (~135 KB livres na home): buffers grandes (imagens, canvas) vão para a **PSRAM**
  (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`).
- Dados fixos (imagens, fontes) são embutidos no firmware por scripts em `tools/` (só Python padrão); a partição do app
  (3 MB) tem ~0,6 MB livres na v1.4. Dados que mudam vão para a NVS (valores pequenos) ou para a LittleFS (arquivos).
- Som: `soundEnable(true)` ao entrar, `soundEnable(false)` ao sair; efeitos novos em `sound.cpp` (`Snd::...`).
- Um comando de uma letra no serial para abrir o app (em `painel.ino`), para testar sem tocar na placa.
