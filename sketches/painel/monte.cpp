#include "monte.h"

#include <ArduinoJson.h>
#include <algorithm>

namespace monte {

bool validWord(const Word &w) {
  std::string joined;
  for (const std::string &s : w.syl) joined += s;
  return !w.syl.empty() && (int)w.syl.size() <= MAX_SLOTS && joined == w.text && w.minLevel >= 1 &&
         w.maxLevel <= LEVELS && w.minLevel <= w.maxLevel;
}

// Vogais com acento (UTF-8) → vogal simples. Só as que aparecem em palavras do jogo.
static const char *const ACCENTS[][2] = {{"Á", "A"}, {"Â", "A"}, {"Ã", "A"}, {"É", "E"}, {"Ê", "E"},
                                         {"Í", "I"}, {"Ó", "O"}, {"Ô", "O"}, {"Õ", "O"}, {"Ú", "U"}};

std::string baseSyllable(const std::string &syl) {
  for (const auto &a : ACCENTS) {
    size_t at = syl.find(a[0]);
    if (at != std::string::npos) return syl.substr(0, at) + a[1] + syl.substr(at + std::string(a[0]).size());
  }
  return syl;
}

std::vector<std::string> allSimpleSyllables() {
  std::vector<std::string> out;
  for (char c : std::string("BCDFGJLMNPRSTVXZ"))
    for (char v : std::string("AEIOU")) out.push_back(std::string(1, c) + v);
  return out;
}

static bool contains(const std::vector<std::string> &v, const std::string &s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}
static char consonant(const std::string &base) { return base.size() == 2 ? base[0] : 0; }
static char vowel(const std::string &base) { return base.empty() ? 0 : base.back(); }

// §6.2 nível 4: BA×PA, TA×DA, FA×VA (só os pares da spec, mesma vogal) ou mesma consoante com outra vogal
static bool similar(const std::string &a, const std::string &b) {
  static const char *const PAIRS[] = {"BP", "TD", "FV"};
  char ca = consonant(a), cb = consonant(b);
  if (ca == cb) return vowel(a) != vowel(b);
  if (vowel(a) != vowel(b)) return false;
  for (const char *p : PAIRS)
    if ((ca == p[0] && cb == p[1]) || (ca == p[1] && cb == p[0])) return true;
  return false;
}

// §6.3 regra 3: com as peças dadas, dá para montar outra palavra da lista com o mesmo número de sílabas?
static bool formsOtherWord(const Word &w, const std::vector<std::string> &pieces, const std::vector<Word> &all) {
  for (const Word &o : all) {
    if (&o == &w || o.text == w.text || o.syl.size() != w.syl.size()) continue;
    std::vector<std::string> left = pieces;
    bool ok = true;
    for (const std::string &s : o.syl) {
      auto it = std::find(left.begin(), left.end(), s);
      if (it == left.end()) {
        ok = false;
        break;
      }
      left.erase(it);
    }
    if (ok) return true;
  }
  return false;
}

template <typename T> static void shuffle(std::vector<T> &v, Rng &rng) {  // Fisher–Yates
  for (int i = (int)v.size() - 1; i > 0; i--) std::swap(v[i], v[rng(i + 1)]);
}

Round makeRound(const Word &w, int level, const std::vector<std::string> &unlocked, const std::vector<Word> &all,
                Rng rng) {
  Round r;
  r.word = &w;
  for (int &s : r.slots) s = -1;

  int want = level == 1 ? 1 : level == 4 ? 2 + (int)rng(2) : 2;  // §6.2 (nível 4: 2 ou 3, sorteado)
  want = std::min(want, MAX_PIECES - (int)w.syl.size());
  std::vector<std::string> bases;
  for (const std::string &s : w.syl) bases.push_back(baseSyllable(s));

  std::vector<std::string> pool;  // candidatas: liberadas, que não são sílaba-alvo (§6.3 regras 1 e 2)
  for (const std::string &u : unlocked) {
    if (contains(bases, u)) continue;
    bool ok = true;
    for (const std::string &b : bases) {
      if (level == 1 && (consonant(u) == consonant(b) || vowel(u) == vowel(b))) ok = false;
    }
    if (level == 4) {
      ok = false;
      for (const std::string &b : bases) ok = ok || similar(u, b);
    }
    if (ok) pool.push_back(u);
  }
  shuffle(pool, rng);

  std::vector<std::string> texts = w.syl;
  for (const std::string &cand : pool) {
    if ((int)texts.size() - (int)w.syl.size() >= want) break;
    std::vector<std::string> with = texts;
    with.push_back(cand);
    if (level < 4 && formsOtherWord(w, with, all)) continue;  // §6.3 regra 3 (permitido no nível 4)
    texts = with;
  }  // poucas candidatas → menos distratores (§6.3 regra 4)

  for (int tries = 0; tries < 10; tries++) {  // §6.4: não deixar os alvos já em ordem no início
    shuffle(texts, rng);
    if (!std::equal(w.syl.begin(), w.syl.end(), texts.begin())) break;
  }
  for (const std::string &t : texts) r.pieces.push_back({t, -1});
  return r;
}

int Round::place(int piece) {
  if (piece < 0 || piece >= (int)pieces.size() || pieces[piece].slot >= 0) return -1;
  for (int s = 0; s < slotCount(); s++) {
    if (slots[s] < 0) {
      slots[s] = piece;
      pieces[piece].slot = s;
      return s;
    }
  }
  return -1;
}

bool Round::remove(int slot) {
  if (slot < 0 || slot >= slotCount() || slots[slot] < 0) return false;
  pieces[slots[slot]].slot = -1;
  slots[slot] = -1;
  return true;
}

Check Round::check() const {
  bool correct = true;
  for (int s = 0; s < slotCount(); s++) {
    if (slots[s] < 0) return Check::Incomplete;
    correct = correct && pieces[slots[s]].text == word->syl[s];  // compara o texto (BA-NA-NA: os NA se trocam)
  }
  return correct ? Check::Correct : Check::Wrong;
}

Hint Round::wrong(std::vector<int> &returned) {
  returned.clear();
  for (int s = 0; s < slotCount(); s++) {
    if (slots[s] >= 0 && pieces[slots[s]].text != word->syl[s]) {
      returned.push_back(slots[s]);
      remove(s);
    }
  }
  errors++;
  if (errors == 2) return Hint::SpeakWord;
  if (errors == 3) {
    visualHint = true;
    return Hint::Blink;
  }
  if (errors >= 4) {
    guided = true;
    return Hint::Guided;
  }
  return Hint::None;
}

int Round::nextCorrectPiece() const {
  for (int s = 0; s < slotCount(); s++) {
    if (slots[s] >= 0) continue;
    for (int p = 0; p < (int)pieces.size(); p++)
      if (pieces[p].slot < 0 && pieces[p].text == word->syl[s]) return p;
    return -1;
  }
  return -1;
}

std::string saveProgress(const Progress &p) {
  JsonDocument doc;
  doc["nivel_atual"] = p.level;
  for (const std::string &s : p.unlocked) doc["silabas_liberadas"].add(s);
  JsonObject words = doc["palavras"].to<JsonObject>();
  for (const auto &kv : p.words) {
    JsonObject o = words[kv.first].to<JsonObject>();
    o["tentativas"] = kv.second.tries;
    o["acertos_sem_ajuda"] = kv.second.noHelp;
    o["com_ajuda"] = kv.second.withHelp;
    o["ultima_vez"] = kv.second.lastSeen;
  }
  JsonArray hist = doc["historico_nivel"].to<JsonArray>();
  for (bool h : p.history) hist.add(h);
  doc["guiadas_seguidas"] = p.guidedStreak;
  for (const std::string &s : p.recent) doc["recentes"].add(s);
  std::string out;
  serializeJson(doc, out);
  return out;
}

Progress loadProgress(const std::string &json, bool *ok) {
  Progress p;
  JsonDocument doc;
  bool good = !deserializeJson(doc, json) && doc["nivel_atual"].is<int>() && doc["silabas_liberadas"].is<JsonArray>();
  if (good) {
    p.level = std::max(1, std::min(LEVELS, doc["nivel_atual"].as<int>()));
    p.unlocked.clear();
    for (JsonVariant s : doc["silabas_liberadas"].as<JsonArray>()) p.unlocked.push_back(s.as<std::string>());
    for (JsonPair kv : doc["palavras"].as<JsonObject>()) {
      WordStats &st = p.words[kv.key().c_str()];
      st.tries = kv.value()["tentativas"] | 0;
      st.noHelp = kv.value()["acertos_sem_ajuda"] | 0;
      st.withHelp = kv.value()["com_ajuda"] | 0;
      st.lastSeen = kv.value()["ultima_vez"] | "";
    }
    for (JsonVariant h : doc["historico_nivel"].as<JsonArray>()) p.history.push_back(h.as<bool>());
    p.guidedStreak = doc["guiadas_seguidas"] | 0;
    for (JsonVariant s : doc["recentes"].as<JsonArray>()) p.recent.push_back(s.as<std::string>());
  }
  if (ok) *ok = good;
  return good ? p : Progress();
}

bool wordUnlocked(const Word &w, const std::vector<std::string> &unlocked) {
  for (const std::string &s : w.syl)
    if (!contains(unlocked, baseSyllable(s))) return false;
  return true;
}

const Word *nextWord(Progress &p, const std::vector<Word> &all, Rng rng) {
  for (int level = p.level; level >= 1; level--) {
    std::vector<const Word *> cands;
    for (const Word &w : all)
      if (w.minLevel <= level && level <= w.maxLevel && wordUnlocked(w, p.unlocked)) cands.push_back(&w);
    if (cands.empty()) continue;
    if (cands.size() > 2) {  // §9.2: fora as 2 últimas sorteadas
      std::vector<const Word *> fresh;
      for (const Word *w : cands)
        if (!contains(p.recent, w->id)) fresh.push_back(w);
      if (!fresh.empty()) cands = fresh;
    }
    std::vector<int> weights;
    int total = 0;
    for (const Word *w : cands) {
      auto it = p.words.find(w->id);
      bool seen = it != p.words.end() && it->second.tries > 0;
      int withHelp = it == p.words.end() ? 0 : it->second.withHelp;
      weights.push_back(std::min(1 + 2 * withHelp + (seen ? 0 : 3), 6));
      total += weights.back();
    }
    int pick = (int)rng(total);
    for (size_t i = 0; i < cands.size(); i++) {
      if (pick < weights[i]) {
        p.recent.push_back(cands[i]->id);
        if (p.recent.size() > 2) p.recent.erase(p.recent.begin());
        return cands[i];
      }
      pick -= weights[i];
    }
  }
  return nullptr;
}

int finishRound(Progress &p, const Round &r, const std::string &today) {
  WordStats &st = p.words[r.word->id];
  st.tries++;
  st.lastSeen = today;
  if (r.guided) st.withHelp++;
  else if (r.noHelp()) st.noHelp++;

  p.history.push_back(r.noHelp());
  if ((int)p.history.size() > UP_WINDOW) p.history.erase(p.history.begin());
  p.guidedStreak = r.guided ? p.guidedStreak + 1 : 0;

  int hits = (int)std::count(p.history.begin(), p.history.end(), true);
  int change = 0;
  if (hits >= UP_HITS && p.level < LEVELS) change = +1;
  else if (p.guidedStreak >= DOWN_GUIDED && p.level > 1) change = -1;
  if (change) {
    p.level += change;
    p.history.clear();
    p.guidedStreak = 0;
  }
  return change;
}

}  // namespace monte
