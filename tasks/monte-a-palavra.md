# App "Monte a Palavra" — plano (aprovado em 2026-10-24)

Spec: `docs/specs/apps/monte-a-palavra.md`. Decisões do responsável em 2026-10-24:

| # | Decisão |
|---|---|
| D1 | Painel (ESP32-Touch-LCD-3.5, C++/LVGL), 5º app da tela inicial |
| D2 | não se aplica (não é o Raspberry Pi) |
| D3 | voz **só da palavra**, gerada no Mac (`say -v Luciana`, pt-BR, já vem no Mac); **toca só ao tocar na imagem** ou em 🔊. Sílabas e frases sem voz |
| D4 | só tocar (sem arrastar) |
| D5 | no erro, as peças certas na posição certa ficam |
| D6 | todas as sílabas simples (consoante + vogal) liberadas no início; ajustáveis no menu do responsável |
| D7 | lista abaixo — **revisar** |
| D8 | sim: nome escrito no fim da sessão ("Muito bem, <nome>!"), cadastrado na página da placa (NVS, fora do git) |
| D9 | Noto Emoji 2D 128 px (mesma origem do Ler) |
| Tela | **paisagem**, como o resto do Painel |
| Som | acerto: sílabas acendem em ordem, depois a palavra é falada e toca o som de acerto do Ler. Erro: **sem som**, as peças erradas balançam e voltam |

## Adaptações da spec que precisam do seu ok
1. **Sem as frases faladas** ("Vamos montar", "Muito bem", "Quase", "Vou te ajudar", fim de sessão): viram texto na tela
   + os sons do Ler. Consequência na dica de 2 erros (§5, "fala a palavra devagar"): vira **falar a palavra de novo**.
2. **Áudio dentro do firmware**, comprimido (IMA ADPCM, 16 kHz): 54 palavras ≈ 300–380 KB (amostras: ~0,8 s por palavra). Cabe nos ~600 KB livres do app, mas sobra pouco (~200 KB).
   Gravar na área de arquivos apagaria capas e desenho, por isso não uso arquivos no cartão/LittleFS para o áudio.
   Imagens de 128 px como no Ler (a spec sugere ~200 px; 128 economiza espaço; dá para aumentar depois).
3. **Palavras em `tools/monte_palavras.txt` → header gerado** (como o Ler), em vez de `palavras.json` lido na placa.
   A validação (§6.5: `junção das sílabas == palavra`) roda no script e também no núcleo, coberta por teste.
4. **Parâmetros do §10 como constantes no código** (5 palavras por sessão, subir 4 de 5, descer 3 guiadas seguidas),
   sem `config.json`, porque não haveria como editá-lo. Volume = o volume geral da ⚙.
5. **Progresso** (`progresso.json`, §9.1) na LittleFS com gravação atômica (arquivo temporário + renomear), como a spec.
6. Botão ⌂ no topo para voltar ao Painel (a spec não tem; todos os apps têm). O ⏸ abre o menu com toque longo de 3 s.

## Tela (480×320, paisagem)
```
┌────────────────────────────────────────────────┐
│ ⌂    ★★★☆☆                              [⏸]   │
│ ┌──────────┐     ┌────┐ ┌────┐ ┌────┐         │  ← espaços (2 ou 3)
│ │          │     │ BO │ │ __ │ │ __ │         │
│ │  imagem  │     └────┘ └────┘ └────┘         │
│ │ (toque = │                                   │
│ │  ouvir)  │     ┌────┐ ┌────┐ ┌────┐         │  ← banco (até 6 peças, 2 linhas)
│ └──────────┘     │ LA │ │ PA │ │    │         │
│    [🔊]          └────┘ └────┘ └────┘         │
└────────────────────────────────────────────────┘
```
Peças de ~80×64 px (≈12 mm) com ≥20 px (≈3 mm) entre elas, mesma cor, letras de 28 px (> 1/12 da altura).

## Lista de palavras proposta (D7) — todas com imagem conferida no Noto Emoji
| Nível | Palavras |
|---|---|
| 1–2 (2 sílabas simples) | BO‑LA ⚽ · PA‑TO 🦆 · GA‑TO 🐱 · DA‑DO 🎲 · FA‑CA 🔪 · VA‑CA 🐮 · BO‑CA 👄 · MO‑TO 🏍 · PI‑PA 🪁 · BO‑TA 👢 · LU‑VA 🧤 · VE‑LA 🕯 · DE‑DO 👆 · SA‑PO 🐸 · SU‑CO 🧃 · FO‑CA 🦭 · LO‑BO 🐺 · MA‑LA 🧳 · CA‑MA 🛏 · BO‑LO 🎂 · BA‑LA 🍬 · LA‑TA 🥫 · FO‑GO 🔥 · MA‑PA 🗺 · PE‑NA 🪶 · LU‑PA 🔍 · LI‑XO 🗑 |
| 3 (3 sílabas simples) | CA‑VA‑LO 🐴 · MA‑CA‑CO 🐒 · SA‑PA‑TO 👞 · BA‑NA‑NA 🍌 · TO‑MA‑TE 🍅 · JA‑NE‑LA 🪟 · PI‑PO‑CA 🍿 · ME‑NI‑NA 👧 · ME‑NI‑NO 👦 · MA‑LE‑TA 💼 · BA‑TA‑TA 🥔 · CA‑ME‑LO 🐫 · PE‑PI‑NO 🥒 · CO‑RU‑JA 🦉 · CO‑ME‑TA ☄ · BO‑NE‑CA 🪆 |
| 4 (som ambíguo, acento) | CA‑FÉ ☕ · BO‑NÉ 🧢 · CA‑SA 🏠 · RA‑TO 🐭 · GE‑LO 🧊 · SO‑FÁ 🛋 · RO‑DA 🛞 · RO‑SA 🌹 · GI‑RA‑FA 🦒 · CE‑BO‑LA 🧅 · JA‑CA‑RÉ 🐊 |

Fora (sem emoji): TATU, PETECA, CANECA. Trocas da spec: DOCE → saiu (a imagem seria igual a BALA).
Dúvidas: LIXO (X tem som de "ch"; a imagem 🗑 é uma lixeira — por isso a voz) e BONECA (🪆 é uma matrioska).

## Arquitetura (§12)
- `monte_core.h/.cpp`: núcleo em C++ puro (sem Arduino), com `rng` recebido por parâmetro e eventos para a tela/som.
- `tests/monte_core_test.cpp`: testes do §14.1 que se aplicam (1–11, 13–16; o 12 `slug` não é necessário sem arquivos),
  compilados no Mac com o `clang++` que já existe (nada a instalar).
- `ui.cpp` (tela), `sound.cpp` (tocar a palavra: voz ADPCM misturada no sintetizador), `tools/monte_assets.py`
  (imagens + áudio: `say` → `afconvert` → ADPCM, só Python padrão).

## Etapas
- [x] 1. Adaptações e lista aprovadas (2026-10-24), sem mudanças; voz Luciana; BONECA fica 🪆 (não há emoji melhor)
- [x] 2. Núcleo (`sketches/painel/monte.*`) + testes no Mac (`tests/monte_test.cpp`, 15 casos, passando; testados contra 3 regras quebradas de propósito)
- [x] 3. `tools/monte_assets.py` (imagens 138 KB + voz ADPCM 248 KB, 32 s) e `soundSpeak` no `sound.cpp`. Firmware 94% (158 KB livres)
- [ ] 4. Tela: rodada, acerto, erro, dicas, sessão de 5, fim com o nome — gravada; falta conferência do usuário
- [ ] 5. Menu do responsável (sílabas, progresso, nível, volume, zerar com dupla confirmação) + nome na página — gravado; falta conferência do usuário
- [x] 6. Specs/README/PROJECT_CONTEXT + commit + release v1.5 (2026-10-24, antes do teste com a filha, a pedido do usuário)

## Interpretações no núcleo (confirmar com o responsável)
- Nível 1 "bem diferente": o distrator não pode ter a consoante **nem** a vogal de **nenhuma** sílaba-alvo.
- Nível 4 "parecidos": só os pares da spec (B×P, T×D, F×V, mesma vogal) ou mesma consoante com outra vogal; se faltarem
  parecidos liberados, usa menos distratores (§6.3 regra 4). Quantidade: 2 ou 3, sorteada.
- Sílaba com acento (FÉ, NÉ) conta como liberada quando a família dela (FE, NE) está liberada.
- `progresso.json` guarda também `guiadas_seguidas` e `recentes` (para descer de nível e não repetir depois de desligar).
