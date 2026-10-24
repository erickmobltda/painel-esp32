// Testes do núcleo do Monte a Palavra (spec §14.1). Rodam no Mac, sem a placa:
//   clang++ -std=c++17 -I arduino/libraries/ArduinoJson/src tests/monte_test.cpp sketches/painel/monte.cpp \
//     -o build/monte_test && build/monte_test
#include "../sketches/painel/monte.h"

#include <algorithm>
#include <cstdio>
#include <deque>

using namespace monte;

static int failures = 0;
#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                \
    }                                                            \
  } while (0)

static Rng seeded(unsigned seed) {  // gerador fixo para resultados repetíveis
  auto state = std::make_shared<unsigned>(seed);
  return [state](unsigned n) {
    *state = *state * 1103515245u + 12345u;
    return (*state >> 8) % n;
  };
}

static const std::vector<Word> WORDS = {
    {"bola", "BOLA", {"BO", "LA"}, 1, 2},          {"pato", "PATO", {"PA", "TO"}, 1, 2},
    {"gato", "GATO", {"GA", "TO"}, 1, 2},          {"dado", "DADO", {"DA", "DO"}, 1, 2},
    {"boneca", "BONECA", {"BO", "NE", "CA"}, 3, 3}, {"banana", "BANANA", {"BA", "NA", "NA"}, 3, 3},
    {"cafe", "CAFÉ", {"CA", "FÉ"}, 4, 4},
};
static const Word &word(const char *id) {
  return *std::find_if(WORDS.begin(), WORDS.end(), [&](const Word &w) { return w.id == id; });
}
static int pieceWith(const Round &r, const char *text) {  // 1ª peça do banco com esse texto
  for (int i = 0; i < (int)r.pieces.size(); i++)
    if (r.pieces[i].slot < 0 && r.pieces[i].text == text) return i;
  return -1;
}

int main() {
  const std::vector<std::string> all = allSimpleSyllables();

  {  // 1. BOLA: BO, LA → acerto
    Round r = makeRound(word("bola"), 2, all, WORDS, seeded(1));
    CHECK(r.place(pieceWith(r, "BO")) == 0);
    CHECK(r.place(pieceWith(r, "LA")) == 1);
    CHECK(r.check() == Check::Correct);
  }
  {  // 2. BOLA: LA, BO → erro; as duas estão erradas e voltam
    Round r = makeRound(word("bola"), 2, all, WORDS, seeded(2));
    r.place(pieceWith(r, "LA"));
    r.place(pieceWith(r, "BO"));
    CHECK(r.check() == Check::Wrong);
    std::vector<int> back;
    r.wrong(back);
    CHECK(back.size() == 2);
    CHECK(r.slots[0] < 0 && r.slots[1] < 0);
  }
  {  // 3. BONECA: BO, CA, NE → erro; BO fica, CA e NE voltam
    Round r = makeRound(word("boneca"), 3, all, WORDS, seeded(3));
    int bo = pieceWith(r, "BO");
    r.place(bo);
    r.place(pieceWith(r, "CA"));
    r.place(pieceWith(r, "NE"));
    CHECK(r.check() == Check::Wrong);
    std::vector<int> back;
    r.wrong(back);
    CHECK(back.size() == 2);
    CHECK(r.slots[0] == bo && r.slots[1] < 0 && r.slots[2] < 0);
  }
  {  // 4. BANANA: os dois NA se trocam
    for (int order = 0; order < 2; order++) {
      Round r = makeRound(word("banana"), 3, all, WORDS, seeded(4));
      std::vector<int> na;
      for (int i = 0; i < (int)r.pieces.size(); i++)
        if (r.pieces[i].text == "NA") na.push_back(i);
      CHECK(na.size() == 2);
      r.place(pieceWith(r, "BA"));
      r.place(na[order]);
      r.place(na[1 - order]);
      CHECK(r.check() == Check::Correct);
    }
  }
  {  // 5. distratores: nunca sílaba-alvo, nunca não liberada (1.000 rodadas)
    std::vector<std::string> some = {"BO", "LA", "PA", "TO", "MI", "VU", "NE", "CA", "BA", "NA", "FE", "DE"};
    Rng rng = seeded(5);
    for (int i = 0; i < 1000; i++) {
      const Word &w = WORDS[i % WORDS.size()];
      int level = std::max(w.minLevel, std::min(w.maxLevel, 1 + i % 4));
      Round r = makeRound(w, level, some, WORDS, rng);
      int targets = 0;
      for (const Piece &p : r.pieces) {
        bool isTarget = std::count(w.syl.begin(), w.syl.end(), p.text) > 0;
        if (isTarget) targets++;
        else CHECK(std::find(some.begin(), some.end(), baseSyllable(p.text)) != some.end());
        for (const std::string &s : w.syl)
          if (!isTarget) CHECK(baseSyllable(p.text) != baseSyllable(s));
      }
      CHECK(targets == (int)w.syl.size());
    }
  }
  {  // 6. alvo PATO com GATO na lista → GA nunca aparece nos níveis 1–2
    Rng rng = seeded(6);
    for (int i = 0; i < 1000; i++) {
      Round r = makeRound(word("pato"), 1 + i % 2, all, WORDS, rng);
      for (const Piece &p : r.pieces) CHECK(p.text != "GA");
    }
  }
  {  // 7. liberadas {BO, LA}, BOLA no nível 2 → 0 distratores
    Round r = makeRound(word("bola"), 2, {"BO", "LA"}, WORDS, seeded(7));
    CHECK(r.pieces.size() == 2);
  }
  {  // 8. embaralhamento: a 1ª tentativa deixaria BO, LA no início → embaralha de novo
    std::deque<unsigned> script = {2, 1};  // Fisher–Yates com 3 peças: troca 2↔2 e 1↔1 = ordem original
    unsigned calls = 0;
    Rng rng = [&](unsigned) {
      calls++;
      unsigned v = script.empty() ? 0 : script.front();
      if (!script.empty()) script.pop_front();
      return v;
    };
    Round r = makeRound(word("bola"), 1, {"BO", "LA", "TI"}, WORDS, rng);
    CHECK(r.pieces.size() == 3);
    CHECK(!(r.pieces[0].text == "BO" && r.pieces[1].text == "LA"));
    CHECK(calls == 4);  // duas tentativas
    // sempre a mesma ordem → para depois de 10 tentativas e aceita
    calls = 0;
    Round r2 = makeRound(word("bola"), 1, {"BO", "LA", "TI"}, WORDS, [&](unsigned n) {
      calls++;
      return n - 1;
    });
    CHECK(calls == 20);
    CHECK(r2.pieces[0].text == "BO");
  }
  {  // 9. dicas: 2 erros → falar; 3 → piscar a próxima certa; 4 → solução guiada (com ajuda)
    Round r = makeRound(word("bola"), 2, all, WORDS, seeded(9));
    std::vector<int> back;
    Hint hints[4];
    for (Hint &h : hints) {
      r.place(pieceWith(r, "LA"));
      r.place(pieceWith(r, "BO"));
      h = r.wrong(back);
    }
    CHECK(hints[0] == Hint::None && hints[1] == Hint::SpeakWord && hints[2] == Hint::Blink &&
          hints[3] == Hint::Guided);
    CHECK(r.nextCorrectPiece() == pieceWith(r, "BO"));
    while (r.check() == Check::Incomplete) r.place(r.nextCorrectPiece());
    CHECK(r.check() == Check::Correct);
    Progress p;
    finishRound(p, r, "2026-10-24");
    CHECK(p.words["bola"].withHelp == 1 && p.words["bola"].noHelp == 0 && p.words["bola"].tries == 1);
  }
  {  // 10. tirar do espaço devolve a peça ao mesmo lugar do banco
    Round r = makeRound(word("boneca"), 3, all, WORDS, seeded(10));
    std::vector<Piece> before = r.pieces;
    int ne = pieceWith(r, "NE");
    CHECK(r.place(ne) == 0);
    CHECK(r.remove(0));
    CHECK(r.pieces[ne].slot == -1 && r.slots[0] == -1);
    for (size_t i = 0; i < before.size(); i++) CHECK(r.pieces[i].text == before[i].text);
  }
  {  // 11. não verifica com espaço vazio
    Round r = makeRound(word("boneca"), 3, all, WORDS, seeded(11));
    r.place(pieceWith(r, "BO"));
    r.place(pieceWith(r, "NE"));
    CHECK(r.check() == Check::Incomplete);
  }
  {  // 13. sílabas que não formam a palavra → rejeitada
    CHECK(validWord(word("cafe")));
    CHECK(!validWord({"bola", "BOLA", {"BO", "LO"}, 1, 2}));
  }
  {  // 14. progresso corrompido → padrão; progresso válido volta igual
    bool ok = true;
    Progress p = loadProgress("{nao e json", &ok);
    CHECK(!ok && p.level == 1 && p.unlocked == allSimpleSyllables());
    p.level = 3;
    p.unlocked = {"BO", "LA"};
    p.words["bola"] = {4, 3, 1, "2026-10-24"};
    p.history = {true, false};
    Progress q = loadProgress(saveProgress(p), &ok);
    CHECK(ok && q.level == 3 && q.unlocked == p.unlocked && q.words["bola"].noHelp == 3 &&
          q.words["bola"].lastSeen == "2026-10-24" && q.history == p.history);
  }
  {  // 15. não repete as 2 últimas com mais de 2 candidatas
    Progress p;
    Rng rng = seeded(15);
    std::vector<std::string> drawn;
    for (int i = 0; i < 1000; i++) {
      const Word *w = nextWord(p, WORDS, rng);
      CHECK(w && w->minLevel <= 1);
      if (drawn.size() >= 1) CHECK(w->id != drawn[drawn.size() - 1]);
      if (drawn.size() >= 2) CHECK(w->id != drawn[drawn.size() - 2]);
      drawn.push_back(w->id);
    }
    Progress none;
    none.unlocked = {"XU"};
    CHECK(nextWord(none, WORDS, rng) == nullptr);  // nada liberado → aviso ao responsável
  }
  {  // 16. sobe com 4 acertos sem ajuda em 5; desce com 3 guiadas seguidas
    Progress p;
    Round ok = makeRound(word("bola"), 1, all, WORDS, seeded(16));
    Round help = ok;
    help.guided = true;
    Round meh = ok;
    meh.errors = 2;
    CHECK(finishRound(p, ok, "") == 0);
    CHECK(finishRound(p, meh, "") == 0);
    CHECK(finishRound(p, ok, "") == 0);
    CHECK(finishRound(p, ok, "") == 0);
    CHECK(finishRound(p, ok, "") == +1 && p.level == 2 && p.history.empty());
    CHECK(finishRound(p, help, "") == 0);
    CHECK(finishRound(p, help, "") == 0);
    CHECK(finishRound(p, help, "") == -1 && p.level == 1);
    CHECK(finishRound(p, help, "") == 0);  // já no nível 1
  }

  std::printf(failures ? "%d falha(s)\n" : "todos os testes passaram\n", failures);
  return failures ? 1 : 0;
}
