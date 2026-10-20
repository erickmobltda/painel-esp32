#include "paint.h"
#include "display.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <LittleFS.h>

static const int SCR_W = 480, SCR_H = 320;
static const int CW = 384, CH = 320;  // área de desenho; a barra de ferramentas ocupa x 384..479
static const int BAR_X = CW, ROW_H = 45, ROW_Y0 = 3;
static const int COL_X[2] = {BAR_X + 24, BAR_X + 72};
static const int CELL_R = 19;
static const int BRUSH_R[3] = {3, 7, 14};
static const int SIZE_X[3] = {BAR_X + 16, BAR_X + 48, BAR_X + 80};
static const char *FILE_PATH = "/desenho.bin";
static const uint32_t AUTOSAVE_MS = 15000;
static const int PUSH_ROWS = 16;

static constexpr uint16_t C(uint32_t rgb) {
  return ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F);
}
static const uint16_t BRANCO = 0xFFFF;
// preto, vermelho, laranja, amarelo, verde, azul, roxo, rosa
static const uint16_t COLORS[8] = {C(0x000000), C(0xE53935), C(0xFB8C00), C(0xFDD835),
                                   C(0x43A047), C(0x1E88E5), C(0x8E24AA), C(0xF06292)};
// purpurina: dourado, prata, rosa, azul-claro, lilás, branco brilhante
static const uint16_t GLITTER[6] = {C(0xFFD700), C(0xC0C0C0), C(0xFF69B4), C(0x87CEFA), C(0xC8A2C8), C(0xFFFFF0)};

enum class Mode { Pen, Glitter, Eraser };
static Mode mode = Mode::Pen;
static int colorIdx = 0, sizeIdx = 1;

static bool running = false;
static uint16_t *canvas = nullptr;   // PSRAM, RGB565 nativo
static uint16_t *pushBuf = nullptr;  // RAM interna (DMA), big-endian para a tela
static Arduino_GFX *gfx = nullptr;
static int dx0, dy0, dx1, dy1;  // retângulo sujo (ainda não enviado)
static bool drawing = false, confirming = false;
static bool waitRelease = false;  // depois de tocar na barra/diálogo, o mesmo dedo não risca
static bool exitPending = false;  // "voltar" sai ao soltar o dedo (o toque não cai na tela inicial)
static int lastX, lastY;
static bool unsaved = false;
static uint32_t lastTouchAt = 0;
// O touch às vezes "solta" por uma leitura no meio do traço: só considera que o dedo saiu após este tempo
static const uint32_t LIFT_MS = 60;
static uint32_t lastChange = 0;

/* ================= tela ================= */
static void markDirty(int x0, int y0, int x1, int y1) {
  dx0 = min(dx0, max(x0, 0));
  dy0 = min(dy0, max(y0, 0));
  dx1 = max(dx1, min(x1, CW - 1));
  dy1 = max(dy1, min(y1, CH - 1));
}
static void clearDirty() {
  dx0 = CW;
  dy0 = CH;
  dx1 = dy1 = -1;
}
// Envia um pedaço da área de desenho: copia invertendo os bytes para a RAM interna e manda por DMA
static void pushRect(int x0, int y0, int x1, int y1) {
  int w = x1 - x0 + 1;
  for (int y = y0; y <= y1; y += PUSH_ROWS) {
    int h = min(PUSH_ROWS, y1 - y + 1);
    for (int r = 0; r < h; r++) {
      const uint16_t *src = canvas + (y + r) * CW + x0;
      uint16_t *dst = pushBuf + r * w;
      for (int i = 0; i < w; i++) dst[i] = __builtin_bswap16(src[i]);
    }
    gfx->draw16bitBeRGBBitmap(x0, y, pushBuf, w, h);
  }
}
static void flushDirty() {
  if (dx1 < dx0) return;
  pushRect(dx0, dy0, dx1, dy1);
  clearDirty();
}

/* ================= pincéis ================= */
static void dot(int cx, int cy, int r, uint16_t col) {
  for (int y = max(cy - r, 0); y <= min(cy + r, CH - 1); y++)
    for (int x = max(cx - r, 0); x <= min(cx + r, CW - 1); x++)
      if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) canvas[y * CW + x] = col;
}
static void stamp(int x, int y) {
  int r = BRUSH_R[sizeIdx];
  if (mode == Mode::Eraser) {
    r *= 2;  // borracha maior que a caneta, mais fácil para criança
    dot(x, y, r, BRANCO);
  } else if (mode == Mode::Glitter) {
    int n = max(2, r / 2), s = r <= 3 ? 0 : 1;
    for (int i = 0; i < n; i++) {
      int ox = (int)(esp_random() % (2 * r + 1)) - r, oy = (int)(esp_random() % (2 * r + 1)) - r;
      if (ox * ox + oy * oy > r * r) continue;
      dot(x + ox, y + oy, s, GLITTER[esp_random() % 6]);
    }
  } else {
    dot(x, y, r, COLORS[colorIdx]);
  }
  markDirty(x - r - 1, y - r - 1, x + r + 1, y + r + 1);
  unsaved = true;
  lastChange = millis();
}
// Liga o ponto anterior ao atual com carimbos próximos (o touch é lido a cada poucos ms)
static void strokeTo(int x, int y) {
  int r = BRUSH_R[sizeIdx] * (mode == Mode::Eraser ? 2 : 1);
  float step = max(1.0f, r / 2.0f);
  float ddx = x - lastX, ddy = y - lastY, d = sqrtf(ddx * ddx + ddy * ddy);
  int steps = max(1, (int)(d / step));
  for (int i = 1; i <= steps; i++) stamp(lastX + ddx * i / steps, lastY + ddy * i / steps);
  lastX = x;
  lastY = y;
}

/* ================= barra de ferramentas ================= */
static int rowY(int row) { return ROW_Y0 + row * ROW_H + ROW_H / 2; }

static void drawCellBox(int cx, int cy, uint16_t bg, bool selected) {
  if (selected) gfx->fillRoundRect(cx - CELL_R - 3, cy - CELL_R - 3, 2 * CELL_R + 6, 2 * CELL_R + 6, 10, BRANCO);
  gfx->fillRoundRect(cx - CELL_R, cy - CELL_R, 2 * CELL_R, 2 * CELL_R, 8, bg);
}
static void drawToolbar() {
  gfx->fillRect(BAR_X, 0, SCR_W - BAR_X, SCR_H, C(0x303030));
  // voltar (casinha)
  int cx = COL_X[0], cy = rowY(0);
  drawCellBox(cx, cy, C(0x1E88E5), false);
  gfx->fillTriangle(cx - 12, cy - 1, cx + 12, cy - 1, cx, cy - 13, BRANCO);
  gfx->fillRect(cx - 8, cy - 1, 16, 12, BRANCO);
  gfx->fillRect(cx - 3, cy + 4, 6, 7, C(0x1E88E5));
  // apagar tudo (lixeira)
  cx = COL_X[1];
  drawCellBox(cx, cy, C(0xC62828), false);
  gfx->fillRect(cx - 10, cy - 9, 20, 3, BRANCO);
  gfx->fillRect(cx - 3, cy - 12, 6, 3, BRANCO);
  gfx->fillRect(cx - 8, cy - 5, 16, 17, BRANCO);
  for (int i = -4; i <= 4; i += 4) gfx->drawFastVLine(cx + i, cy - 2, 11, C(0xC62828));
  // cores
  for (int i = 0; i < 8; i++) {
    int x = COL_X[i % 2], y = rowY(1 + i / 2);
    if (mode == Mode::Pen && i == colorIdx) gfx->fillCircle(x, y, CELL_R + 3, BRANCO);
    gfx->fillCircle(x, y, CELL_R, COLORS[i]);
    if (i == 0) gfx->drawCircle(x, y, CELL_R, C(0x808080));  // preto visível no fundo escuro
  }
  // glitter
  cx = COL_X[0];
  cy = rowY(5);
  drawCellBox(cx, cy, C(0x101030), mode == Mode::Glitter);
  static const int8_t sparkle[][2] = {{-10, -8}, {6, -11}, {-4, 2}, {9, 4}, {-11, 9}, {2, 10}, {11, -2}, {-7, -1}};
  for (int i = 0; i < 8; i++) gfx->fillRect(cx + sparkle[i][0], cy + sparkle[i][1], 3, 3, GLITTER[i % 6]);
  gfx->drawFastHLine(cx - 6, cy - 4, 13, C(0xFFD700));  // estrela
  gfx->drawFastVLine(cx, cy - 10, 13, C(0xFFD700));
  // borracha
  cx = COL_X[1];
  drawCellBox(cx, cy, C(0x606060), mode == Mode::Eraser);
  gfx->fillRect(cx - 12, cy - 7, 14, 14, C(0xF48FB1));
  gfx->fillRect(cx + 2, cy - 7, 10, 14, BRANCO);
  // espessuras
  for (int i = 0; i < 3; i++) {
    int x = SIZE_X[i], y = rowY(6);
    if (i == sizeIdx) gfx->fillCircle(x, y, 14, BRANCO);
    gfx->fillCircle(x, y, 12, C(0x505050));
    gfx->fillCircle(x, y, BRUSH_R[i] * 2 / 3 + 1, mode == Mode::Pen ? COLORS[colorIdx] : BRANCO);
  }
}

/* ================= confirmação de "apagar tudo" ================= */
static const int DLG_X = 52, DLG_Y = 80, DLG_W = 280, DLG_H = 160;
static const int YES_X = DLG_X + 20, NO_X = DLG_X + 150, BTN_Y = DLG_Y + 85, BTN_W = 110, BTN_H = 55;

static void drawButtonText(int x, int y, int w, int h, uint16_t bg, const char *text) {
  gfx->fillRoundRect(x, y, w, h, 10, bg);
  gfx->setTextSize(3);
  gfx->setTextColor(BRANCO);
  gfx->setCursor(x + (w - strlen(text) * 18) / 2, y + (h - 21) / 2);
  gfx->print(text);
}
static void showConfirm() {
  confirming = true;
  gfx->fillRoundRect(DLG_X, DLG_Y, DLG_W, DLG_H, 14, C(0x1a2250));
  gfx->setTextSize(3);
  gfx->setTextColor(BRANCO);
  gfx->setCursor(DLG_X + 32, DLG_Y + 28);
  gfx->print("Apagar tudo?");
  drawButtonText(YES_X, BTN_Y, BTN_W, BTN_H, C(0xC62828), "Sim");
  drawButtonText(NO_X, BTN_Y, BTN_W, BTN_H, C(0x606060), "Voltar");
}
static void closeConfirm(bool erase) {
  confirming = false;
  if (erase) {
    for (int i = 0; i < CW * CH; i++) canvas[i] = BRANCO;
    unsaved = true;
    lastChange = millis();
    pushRect(0, 0, CW - 1, CH - 1);
  } else {
    pushRect(DLG_X, DLG_Y, DLG_X + DLG_W - 1, DLG_Y + DLG_H - 1);  // redesenha o que estava embaixo
  }
}
static bool inRect(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }

/* ================= salvar / carregar (RLE: pares quantidade + cor) ================= */
static void save() {
  uint32_t t0 = millis();
  File f = LittleFS.open(FILE_PATH, "w");
  if (!f) {
    Serial.println("[pintar] nao foi possivel salvar");
    return;
  }
  f.write((const uint8_t *)"PNT1", 4);
  static uint16_t pairs[256];
  int n = 0;
  for (int i = 0; i < CW * CH;) {
    uint16_t col = canvas[i];
    int run = 1;
    while (i + run < CW * CH && canvas[i + run] == col && run < 65535) run++;
    pairs[n++] = run;
    pairs[n++] = col;
    if (n == 256) {
      f.write((const uint8_t *)pairs, sizeof(pairs));
      n = 0;
    }
    i += run;
  }
  if (n) f.write((const uint8_t *)pairs, n * 2);
  size_t size = f.position();
  f.close();
  unsaved = false;
  Serial.printf("[pintar] salvo (%u bytes) em %u ms\n", size, millis() - t0);
}
static void load() {
  for (int i = 0; i < CW * CH; i++) canvas[i] = BRANCO;
  File f = LittleFS.open(FILE_PATH, "r");
  if (!f) return;
  char magic[4];
  if (f.read((uint8_t *)magic, 4) == 4 && memcmp(magic, "PNT1", 4) == 0) {
    uint16_t pair[2];
    int i = 0;
    while (i < CW * CH && f.read((uint8_t *)pair, 4) == 4)
      for (int k = 0; k < pair[0] && i < CW * CH; k++) canvas[i++] = pair[1];
  }
  f.close();
}

/* ================= ciclo ================= */
void paintStart() {
  canvas = (uint16_t *)heap_caps_malloc(CW * CH * 2, MALLOC_CAP_SPIRAM);
  pushBuf = (uint16_t *)heap_caps_malloc(CW * PUSH_ROWS * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (!canvas || !pushBuf) {
    Serial.println("[pintar] sem memoria");
    free(canvas);
    free(pushBuf);
    canvas = pushBuf = nullptr;
    return;
  }
  if (!LittleFS.begin(true)) Serial.println("[pintar] LittleFS indisponivel: o desenho nao sera salvo");
  load();
  gfx = displayGfx();
  drawToolbar();
  pushRect(0, 0, CW - 1, CH - 1);
  clearDirty();
  drawing = confirming = unsaved = waitRelease = exitPending = false;
  running = true;
  Serial.printf("[pintar] aberto | heap interno %u, psram %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

bool paintRunning() { return running; }

static bool requested = false;
void paintRequestStart() { requested = true; }
void paintStartIfRequested() {
  if (!requested) return;
  requested = false;
  paintStart();
}

// Toque na barra (só na borda de descida). Devolve false se for "voltar".
static bool barTap(int x, int y) {
  int row = (y - ROW_Y0) / ROW_H, col = x < BAR_X + 48 ? 0 : 1;
  if (row <= 0) {
    if (col == 0) return false;
    showConfirm();
    return true;
  }
  if (row >= 1 && row <= 4) {
    colorIdx = (row - 1) * 2 + col;
    mode = Mode::Pen;
  } else if (row == 5) {
    mode = col == 0 ? Mode::Glitter : Mode::Eraser;
  } else {
    sizeIdx = x < BAR_X + 32 ? 0 : x < BAR_X + 64 ? 1 : 2;
  }
  drawToolbar();
  return true;
}

bool paintFrame() {
  int16_t x, y;
  bool touched = displayReadTouch(&x, &y, 1) > 0;

  if (!touched) {
    if (drawing && millis() - lastTouchAt < LIFT_MS) {
      delay(2);  // talvez seja só uma leitura falhada: espera a próxima sem quebrar o traço
      return true;
    }
    drawing = false;
    waitRelease = false;
    if (exitPending) return false;
    if (unsaved && millis() - lastChange > AUTOSAVE_MS) save();
    delay(10);
    return true;
  }
  lastTouchAt = millis();
  if (waitRelease) return true;

  if (confirming) {
    if (inRect(x, y, YES_X, BTN_Y, BTN_W, BTN_H)) closeConfirm(true);
    else if (inRect(x, y, NO_X, BTN_Y, BTN_W, BTN_H)) closeConfirm(false);
    waitRelease = true;
    return true;
  }

  if (x < CW) {
    if (drawing) strokeTo(x, y);
    else {
      lastX = x;
      lastY = y;
      stamp(x, y);
    }
    drawing = true;
    flushDirty();
  } else if (!drawing) {  // barra: só um toque novo (riscar até a barra não aperta botão)
    if (!barTap(x, y)) exitPending = true;
    waitRelease = true;
  }
  return true;
}

void paintStop() {
  running = false;
  if (unsaved) save();
  free(canvas);
  free(pushBuf);
  canvas = pushBuf = nullptr;
  Serial.println("[pintar] fechado, memoria liberada");
}
