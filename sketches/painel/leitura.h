#pragma once

#include <stdint.h>

// App Ler: aparece uma palavra e 3 imagens; a criança toca na imagem da palavra.
// Palavras e imagens em leitura_assets.h (gerado por tools/leitura_assets.py a partir de tools/leitura_palavras.txt).
static const int LEITURA_IMG = 128;            // lado das imagens
static const int LEITURA_STARS_PER_LEVEL = 10;  // acertos de primeira para passar de nível

struct LeituraRound {
  int target;      // palavra da rodada
  int options[3];  // palavras das 3 imagens, já na ordem da tela (uma delas é a target)
};

void leituraBegin();  // nível e estrelas salvos na NVS
int leituraLevel();
int leituraLevels();
int leituraStars();
void leituraNewRound(LeituraRound &r);
// Acerto de primeira: +1 estrela. Devolve true quando passa de nível (as estrelas voltam a 0).
bool leituraScore();
// Para o adulto (toque longo no nível): próximo nível, volta ao 1 depois do último. Zera as estrelas.
void leituraNextLevel();
const char *leituraWord(int word);
// RGB565 LEITURA_IMG x LEITURA_IMG na PSRAM (liberar com free). nullptr se faltar memória.
uint16_t *leituraImage(int word);
