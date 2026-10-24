#include "leitura.h"
#include "leitura_assets.h"

#include <Arduino.h>
#include <Preferences.h>
#include <miniz.h>  // tinfl (descompressor zlib) da ROM do ESP32

static_assert(LE_IMG_SIZE == LEITURA_IMG, "rode tools/leitura_assets.py de novo");

static int level = 1, stars = 0, lastTarget = -1;

static void save() {
  Preferences prefs;
  prefs.begin("painel", false);
  prefs.putInt("le_level", level);
  prefs.putInt("le_stars", stars);
  prefs.end();
}

void leituraBegin() {
  Preferences prefs;
  prefs.begin("painel", true);
  level = constrain(prefs.getInt("le_level", 1), 1, LE_LEVELS);
  stars = constrain(prefs.getInt("le_stars", 0), 0, LEITURA_STARS_PER_LEVEL - 1);
  prefs.end();
}

int leituraLevel() { return level; }
int leituraLevels() { return LE_LEVELS; }
int leituraStars() { return stars; }

void leituraNewRound(LeituraRound &r) {
  int words[LE_WORD_COUNT], count = 0;
  for (int i = 0; i < LE_WORD_COUNT; i++)
    if (LE_WORDS[i].level == level && i != lastTarget) words[count++] = i;
  r.target = lastTarget = words[esp_random() % count];
  // 2 erradas entre as LE_NEAR mais parecidas (sorteadas para variar)
  const uint8_t *near = LE_WORDS[r.target].near;
  int a = esp_random() % LE_NEAR, b = (a + 1 + esp_random() % (LE_NEAR - 1)) % LE_NEAR;
  int opts[3] = {r.target, near[a], near[b]};
  for (int i = 2; i > 0; i--) {  // embaralha as posições
    int j = esp_random() % (i + 1);
    int t = opts[i];
    opts[i] = opts[j];
    opts[j] = t;
  }
  memcpy(r.options, opts, sizeof(opts));
}

bool leituraScore() {
  bool levelUp = ++stars >= LEITURA_STARS_PER_LEVEL;
  if (levelUp) {
    stars = 0;
    if (level < LE_LEVELS) level++;  // no último nível continua nele
    lastTarget = -1;
  }
  save();
  return levelUp;
}

void leituraNextLevel() {
  level = level % LE_LEVELS + 1;
  stars = 0;
  lastTarget = -1;
  save();
}

const char *leituraWord(int word) { return LE_WORDS[word].text; }

uint16_t *leituraImage(int word) {
  size_t size = LE_IMG_SIZE * LE_IMG_SIZE * 2;
  uint16_t *px = (uint16_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
  if (!px) return nullptr;
  // O estado do tinfl tem ~11 KB: na PSRAM, não na pilha (tinfl_decompress_mem_to_mem o põe na pilha)
  tinfl_decompressor *inflator = (tinfl_decompressor *)heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_SPIRAM);
  if (!inflator) {
    free(px);
    return nullptr;
  }
  tinfl_init(inflator);
  size_t inLen = LE_WORDS[word].imgLen, outLen = size;
  tinfl_status status = tinfl_decompress(inflator, LE_WORDS[word].img, &inLen, (uint8_t *)px, (uint8_t *)px, &outLen,
                                         TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  free(inflator);
  if (status != TINFL_STATUS_DONE || outLen != size) {
    Serial.printf("[ler] imagem de %s corrompida (status %d)\n", LE_WORDS[word].text, (int)status);
    free(px);
    return nullptr;
  }
  return px;
}
