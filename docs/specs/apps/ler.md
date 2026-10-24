# App Ler

Código: `leitura.*` (regras), `ui.cpp` (tela, `uiShowReading`), `leitura_assets.h` (gerado), `font_le_64.c`.
Serial: `l` abre.

## Objetivo
Ajudar uma criança que já lê sílabas simples (ainda sem 100% de facilidade) a ler palavras inteiras. Sem voz e sem
reconhecimento de fala: ela lê a palavra e toca na imagem certa.

## Tela
- Topo: ⌂ (início), **10 bolinhas** de estrela (amarelas = ganhas no nível) e "Nível N" à direita.
- Centro: a palavra em **MAIÚSCULAS** (Montserrat 64 px, letras um pouco separadas).
- Embaixo: **3 cartões brancos** de 144 px com imagens de 128 px, em posição sorteada.

## Rodada
1. Sorteia uma palavra do nível atual, diferente da anterior.
2. As 2 erradas são sorteadas entre as **4 mais parecidas** do mesmo nível (menos letras diferentes; no empate, mesma
   letra inicial), para não dar para acertar só pela primeira letra. Ex.: BOLA → BOLO, BALA.
3. **Errou:** som `Wrong` (dois tons descendo, suave), o cartão fica apagado (30%) e não pode mais ser tocado; ela tenta
   de novo. Não perde nada.
4. **Acertou:** moldura verde. Se foi **de primeira**, ganha 1 estrela. Som `Correct` (arpejo dó-mi-sol-dó) e nova
   rodada em 1,2 s. Toques são ignorados até a próxima rodada.
5. **10 estrelas:** som `LevelUp` (fanfarra), faixa roxa "✓ Nível N!" por 2,5 s, sobe de nível e as estrelas voltam a 0.
   No último nível continua nele.

Sem cronômetro, sem vidas. O som toca mesmo com o mudo do jogo ligado.

## Para o adulto
- **Toque longo em "Nível N"**: passa para o próximo nível (depois do último volta ao 1) e zera as estrelas.
- Nível e estrelas salvos na NVS (`le_level`, `le_stars`).

## Palavras e imagens
- Lista em `tools/leitura_palavras.txt` (`nível PALAVRA código-do-emoji`), revisada pelo responsável em 2026-10-22:
  95 palavras — nível 1: 39 (2 sílabas simples, ex.: BOLA, GATO), nível 2: 21 (3 sílabas, ex.: SAPATO), nível 3: 35
  (sílabas mais difíceis e palavras longas, ex.: CHAVE, BORBOLETA). Palavra nova só com aprovação do responsável.
- Imagens: Noto Emoji 2D do Google, PNG 128 px (licenças em [`licenses/`](../../../licenses/README.md)).
- `python3 tools/leitura_assets.py` (só Python padrão) baixa as PNGs para `build/leitura_png/`, põe fundo branco,
  converte para RGB565, comprime com zlib (~250 KB no total) e gera `leitura_assets.h` com as palavras parecidas.
- Na placa, só as 3 imagens da rodada são descomprimidas (tinfl da ROM) para a PSRAM e liberadas na rodada seguinte.
  Funciona sem internet.

## Fonte
`font_le_64.c`: Montserrat 64 px só com A–Z e Á Â Ã Ç É Ê Í Ó Ô Õ Ú, gerada com `npx lv_font_conv@1.5.2`. Palavra com
outra letra precisa regerar a fonte.
