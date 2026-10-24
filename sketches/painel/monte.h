#pragma once

// Núcleo do app "Monte a Palavra" (spec: docs/specs/apps/monte-a-palavra.md).
// C++ puro, sem Arduino: roda na placa e nos testes do Mac (tests/monte_test.cpp). O sorteio vem por parâmetro.
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace monte {

static const int SESSION_WORDS = 5;    // §10 palavras_por_sessao
static const int UP_HITS = 4;          // §10 subir_nivel_acertos
static const int UP_WINDOW = 5;        // §10 subir_nivel_janela
static const int DOWN_GUIDED = 3;      // §10 descer_nivel_guiadas_seguidas
static const int LEVELS = 4;
static const int MAX_SLOTS = 3;
static const int MAX_PIECES = 6;

using Rng = std::function<unsigned(unsigned n)>;  // número sorteado em [0, n)

struct Word {
  std::string id, text;
  std::vector<std::string> syl;  // em ordem; a junção é igual a text
  int minLevel, maxLevel;        // níveis em que a palavra aparece
};
bool validWord(const Word &w);  // §6.5: junção das sílabas == palavra

// Família de uma sílaba: sem o acento da vogal ("FÉ" → "FE"). As liberadas são guardadas por família.
std::string baseSyllable(const std::string &syl);
std::vector<std::string> allSimpleSyllables();  // BA BE BI BO BU CA ... ZU (padrão D6: todas liberadas)

struct Piece {
  std::string text;
  int slot = -1;  // -1 = no banco (a posição no banco é o índice da peça e não muda)
};

enum class Check { Incomplete, Correct, Wrong };
enum class Hint { None, SpeakWord, Blink, Guided };  // §5, conforme os erros da rodada

struct Round {
  const Word *word = nullptr;
  std::vector<Piece> pieces;  // alvos + distratores, embaralhados
  int slots[MAX_SLOTS];       // índice da peça em cada espaço, -1 = vazio
  int errors = 0;
  bool visualHint = false, guided = false;

  int slotCount() const { return (int)word->syl.size(); }
  int place(int piece);      // peça do banco → 1º espaço vazio; devolve o espaço ou -1
  bool remove(int slot);     // peça do espaço → volta ao seu lugar no banco
  Check check() const;       // Incomplete enquanto houver espaço vazio
  // Depois de um Wrong: devolve ao banco as peças erradas (as certas ficam, D5), conta o erro e diz a dica.
  Hint wrong(std::vector<int> &returned);
  int nextCorrectPiece() const;  // peça do banco que vai no 1º espaço vazio (dica que pisca e solução guiada)
  bool noHelp() const { return errors <= 1 && !visualHint && !guided; }  // §9.1 acerto sem ajuda
};

// §6.2–6.4: espaços vazios + banco com distratores das sílabas liberadas, embaralhado.
Round makeRound(const Word &w, int level, const std::vector<std::string> &unlocked, const std::vector<Word> &all,
                Rng rng);

struct WordStats {
  int tries = 0, noHelp = 0, withHelp = 0;
  std::string lastSeen;  // AAAA-MM-DD
};
struct Progress {
  int level = 1;
  std::vector<std::string> unlocked = allSimpleSyllables();
  std::map<std::string, WordStats> words;
  std::vector<bool> history;  // acertos sem ajuda no nível atual, mais antigo primeiro
  int guidedStreak = 0;
  std::vector<std::string> recent;  // últimas palavras sorteadas (não repetir as 2 últimas)
};
std::string saveProgress(const Progress &p);
Progress loadProgress(const std::string &json, bool *ok = nullptr);  // inválido → padrão (§9.1)

bool wordUnlocked(const Word &w, const std::vector<std::string> &unlocked);
// §9.2: sorteio ponderado; sem candidata no nível, tenta os anteriores. nullptr = liberar mais sílabas.
const Word *nextWord(Progress &p, const std::vector<Word> &all, Rng rng);
// Registra o fim da rodada e sobe/desce de nível (§6.2). Devolve +1, -1 ou 0.
int finishRound(Progress &p, const Round &r, const std::string &today);

}  // namespace monte
