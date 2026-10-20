// O Arduino compila com -Os (tamanho); o laço de desenho do jogo precisa de velocidade
#pragma GCC optimize("O2")

#include "game.h"
#include "game_atlas.h"
#include "display.h"
#include "net.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include <math.h>

// Os nomes, números e a ordem das funções seguem o index.html original para facilitar a comparação.

/* ================= CONFIG ================= */
static const int W = 480, H = 270;   // tela lógica do original
static const int SCR_H = 320;        // a placa tem 50 linhas a mais: viram terra (onde ficam os botões)
static const int GY = 214;           // superfície do chão
static const int PX = 150;           // x do centro da jogadora
static const int MAX_LIVES = 5;
static const float GRAV = 1100, JUMP_H = 70;
static const float V0 = sqrtf(2 * GRAV * JUMP_H);
static const float PREP = 0.10f, LAND = 0.25f, INV_TIME = 1.6f;

static const int BAND_H = 32;  // a tela é desenhada em 10 faixas de 32 linhas (buffer na RAM interna)
static const uint16_t KEY = ATLAS_KEY;

/* ================= HELPERS ================= */
static constexpr uint16_t C(uint32_t rgb) {
  return ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F);
}
static float rnd(float a, float b) { return a + (esp_random() / 4294967296.0f) * (b - a); }
static int jsRound(float v) { return (int)floorf(v + 0.5f); }

struct Img {
  int w = 0, h = 0;
  uint16_t *px = nullptr;  // PSRAM; KEY = transparente
};

static Img mk(int w, int h) {
  Img i;
  i.w = w;
  i.h = h;
  i.px = (uint16_t *)heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM);
  for (int k = 0; k < w * h; k++) i.px[k] = KEY;
  return i;
}
static void freeImg(Img &i) {
  free(i.px);
  i.px = nullptr;
}
static void pix(Img &g, int x, int y, uint16_t col) {
  if (x >= 0 && y >= 0 && x < g.w && y < g.h) g.px[y * g.w + x] = col;
}

/* ================= "CANVAS" EM FAIXAS =================
 * render() é chamado uma vez por faixa; todas as primitivas recortam na faixa atual. */
static uint16_t *band = nullptr;
static int bandY0 = 0;
static int tx = 0;  // ctx.translate (tremida)

static void fillRect(int x, int y, int w, int h, uint16_t col) {
  x += tx;
  int x0 = max(x, 0), x1 = min(x + w, W), y0 = max(y, bandY0), y1 = min(y + h, bandY0 + BAND_H);
  for (int yy = y0; yy < y1; yy++) {
    uint16_t *row = band + (yy - bandY0) * W;
    for (int xx = x0; xx < x1; xx++) row[xx] = col;
  }
}
// Escurece (rgba(0,0,0,a)): keep = (1 - a) * 256
static void darkenRect(int x, int y, int w, int h, int keep) {
  x += tx;
  int x0 = max(x, 0), x1 = min(x + w, W), y0 = max(y, bandY0), y1 = min(y + h, bandY0 + BAND_H);
  for (int yy = y0; yy < y1; yy++) {
    uint16_t *row = band + (yy - bandY0) * W;
    for (int xx = x0; xx < x1; xx++) {
      uint16_t c = row[xx];
      int r = ((c >> 11) * keep) >> 8, g = (((c >> 5) & 0x3F) * keep) >> 8, b = ((c & 0x1F) * keep) >> 8;
      row[xx] = (r << 11) | (g << 5) | b;
    }
  }
}
// Mistura uma cor por cima (alpha 0-256)
static void blendRect(int x, int y, int w, int h, uint16_t col, int alpha) {
  x += tx;
  int x0 = max(x, 0), x1 = min(x + w, W), y0 = max(y, bandY0), y1 = min(y + h, bandY0 + BAND_H);
  int cr = col >> 11, cg = (col >> 5) & 0x3F, cb = col & 0x1F, keep = 256 - alpha;
  for (int yy = y0; yy < y1; yy++) {
    uint16_t *row = band + (yy - bandY0) * W;
    for (int xx = x0; xx < x1; xx++) {
      uint16_t c = row[xx];
      int r = ((c >> 11) * keep + cr * alpha) >> 8, g = (((c >> 5) & 0x3F) * keep + cg * alpha) >> 8,
          b = ((c & 0x1F) * keep + cb * alpha) >> 8;
      row[xx] = (r << 11) | (g << 5) | b;
    }
  }
}
// ctx.drawImage(src, sx, sy, w, h, dx, dy, w, h) com transparência por cor-chave
static void blit(const uint16_t *src, int stride, int sx, int sy, int w, int h, int dx, int dy) {
  dx += tx;
  int y0 = max(dy, bandY0), y1 = min(dy + h, bandY0 + BAND_H);
  int x0 = max(dx, 0), x1 = min(dx + w, W);
  if (y0 >= y1 || x0 >= x1) return;
  for (int yy = y0; yy < y1; yy++) {
    const uint16_t *s = src + (sy + yy - dy) * stride + sx + (x0 - dx);
    uint16_t *d = band + (yy - bandY0) * W + x0;
    for (int n = x1 - x0; n > 0; n--, s++, d++)
      if (*s != KEY) *d = *s;
  }
}
static void drawImg(const Img &i, int dx, int dy) { blit(i.px, i.w, 0, 0, i.w, i.h, dx, dy); }

// Camada grande e com muita transparência (nuvens, morros): guarda só os trechos opacos de cada linha
// e copia cada trecho de uma vez (memcpy), em vez de testar a cor-chave pixel a pixel.
struct Span {
  uint16_t y, x, len;
};
struct SpanImg {
  int w = 0, h = 0;
  uint16_t *px = nullptr;  // pixels (PSRAM)
  Span *spans = nullptr;   // ordenados por linha
  uint16_t *rowStart = nullptr;  // índice do primeiro trecho de cada linha (h + 1 entradas)
};
static SpanImg toSpans(Img &img) {
  SpanImg r;
  r.w = img.w;
  r.h = img.h;
  r.px = img.px;
  img.px = nullptr;  // a SpanImg passa a ser dona dos pixels
  int count = 0;
  for (int pass = 0; pass < 2; pass++) {
    count = 0;
    for (int y = 0; y < r.h; y++) {
      if (pass) r.rowStart[y] = count;
      for (int x = 0; x < r.w;) {
        if (r.px[y * r.w + x] == KEY) {
          x++;
          continue;
        }
        int x0 = x;
        while (x < r.w && r.px[y * r.w + x] != KEY) x++;
        if (pass) r.spans[count] = {(uint16_t)y, (uint16_t)x0, (uint16_t)(x - x0)};
        count++;
      }
    }
    if (!pass) {
      r.spans = (Span *)heap_caps_malloc(count * sizeof(Span), MALLOC_CAP_SPIRAM);
      r.rowStart = (uint16_t *)heap_caps_malloc((r.h + 1) * 2, MALLOC_CAP_SPIRAM);
    }
  }
  r.rowStart[r.h] = count;
  return r;
}
static void freeSpans(SpanImg &r) {
  free(r.px);
  free(r.spans);
  free(r.rowStart);
  r.px = nullptr;
  r.spans = nullptr;
  r.rowStart = nullptr;
}
static void drawSpans(const SpanImg &r, int dx, int dy) {
  dx += tx;
  int y0 = max(dy, bandY0), y1 = min(dy + r.h, bandY0 + BAND_H);
  if (y0 >= y1 || dx >= W || dx + r.w <= 0) return;
  for (int i = r.rowStart[y0 - dy]; i < r.rowStart[y1 - dy]; i++) {
    const Span &sp = r.spans[i];
    int a = max(dx + sp.x, 0), b = min(dx + sp.x + sp.len, W);
    if (a < b) memcpy(band + (sp.y + dy - bandY0) * W + a, r.px + sp.y * r.w + (a - dx), (b - a) * 2);
  }
}
// Amplia (pixel art, vizinho mais próximo) uma vez só, na inicialização
static Img scaled(const Img &src, int scale) {
  Img d = mk(src.w * scale, src.h * scale);
  for (int y = 0; y < d.h; y++)
    for (int x = 0; x < d.w; x++) d.px[y * d.w + x] = src.px[(y / scale) * src.w + x / scale];
  return d;
}

/* ================= PIXEL FONT 5x7 ================= */
// '~' representa o "Ã" do original (A com til)
static const char *glyph(char ch) {
  switch (ch) {
    case 'A': return ".###.#...##...#######...##...##...#";
    case 'B': return "####.#...##...#####.#...##...#####.";
    case 'C': return ".###.#...##....#....#....#...#.###.";
    case 'D': return "####.#...##...##...##...##...#####.";
    case 'E': return "######....#....####.#....#....#####";
    case 'G': return ".###.#...##....#.####...##...#.####";
    case 'I': return ".###...#....#....#....#....#...###.";
    case 'J': return "..###...#....#....#....#.#..#..##..";
    case 'L': return "#....#....#....#....#....#....#####";
    case 'M': return "#...###.###.#.##.#.##...##...##...#";
    case 'N': return "#...###..###..##.#.##..###..###...#";
    case 'O': return ".###.#...##...##...##...##...#.###.";
    case 'P': return "####.#...##...#####.#....#....#....";
    case 'R': return "####.#...##...#####.#.#..#..#.#...#";
    case 'S': return ".#####....#.....###.....#....#####.";
    case 'T': return "#####..#....#....#....#....#....#..";
    case 'U': return "#...##...##...##...##...##...#.###.";
    case 'V': return "#...##...##...##...##...#.#.#...#..";
    case '0': return ".###.#...##..###.#.###..##...#.###.";
    case '1': return "..#...##....#....#....#....#...###.";
    case '2': return ".###.#...#....#...#...#...#...#####";
    case '3': return "####.....#....#.###.....#....#####.";
    case '4': return "#...##...##...######....#....#....#";
    case '5': return "######....####.....#....##...#.###.";
    case '6': return ".###.#....#....####.#...##...#.###.";
    case '7': return "#####....#...#...#...#....#....#...";
    case '8': return ".###.#...##...#.###.#...##...#.###.";
    case '9': return ".###.#...##...#.####....#....#.###.";
    case 'X': return "#...##...#.#.#...#...#.#.#...##...#";
    case '!': return "..#....#....#....#....#.........#..";
    default: return nullptr;
  }
}
static int textW(const char *s, int sc) { return strlen(s) * 6 * sc - sc; }
static void drawText(const char *s, int x, int y, int sc, uint16_t col, bool hasShadow, uint16_t shadow = 0) {
  // fora da faixa sendo desenhada (o til sobe 3 linhas; a sombra desce 1): nada a fazer
  if (y + 8 * sc <= bandY0 || y - 3 * sc >= bandY0 + BAND_H) return;
  int cx = x;
  for (; *s; s++) {
    char chr = *s;
    bool tilde = chr == '~';
    if (tilde) chr = 'A';
    const char *rows = glyph(chr);
    if (rows) {
      if (tilde) {
        static const char *tl[2] = {".##.#", "#.##."};
        for (int tr = 0; tr < 2; tr++)
          for (int tc = 0; tc < 5; tc++)
            if (tl[tr][tc] == '#') {
              if (hasShadow) fillRect(cx + tc * sc, y + (tr - 3) * sc + sc, sc, sc, shadow);
              fillRect(cx + tc * sc, y + (tr - 3) * sc, sc, sc, col);
            }
      }
      if (hasShadow)
        for (int r = 0; r < 7; r++)
          for (int c = 0; c < 5; c++)
            if (rows[r * 5 + c] == '#') fillRect(cx + c * sc, y + r * sc + sc, sc, sc, shadow);
      for (int r = 0; r < 7; r++)
        for (int c = 0; c < 5; c++)
          if (rows[r * 5 + c] == '#') fillRect(cx + c * sc, y + r * sc, sc, sc, col);
    }
    cx += 6 * sc;
  }
}
static void drawTextC(const char *s, int y, int sc, uint16_t col, uint16_t shadow) {
  drawText(s, jsRound((W - textW(s, sc)) / 2.0f), y, sc, col, true, shadow);
}

/* ================= PROCEDURAL PIXEL ART ================= */
template <typename F>
static Img paint(int w, int h, F colorAt) {
  Img c = mk(w, h);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) c.px[y * w + x] = colorAt(x, y);
  return c;
}
template <typename F>
static bool edgeOf(F inside, int x, int y) {
  return !inside(x - 1, y) || !inside(x + 1, y) || !inside(x, y - 1) || !inside(x, y + 1);
}

static Img makeBush() {
  const int w = 30, h = 18;
  static const float cs[3][3] = {{8, 11, 7}, {15, 8, 8}, {23, 11, 7}};
  auto inside = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= w || y >= h) return false;
    if (y >= 12 && x >= 2 && x <= 27) return true;
    for (int i = 0; i < 3; i++) {
      float dx = x + 0.5f - cs[i][0], dy = y + 0.5f - cs[i][1];
      if (dx * dx + dy * dy <= cs[i][2] * cs[i][2]) return true;
    }
    return false;
  };
  return paint(w, h, [&](int x, int y) -> uint16_t {
    if (!inside(x, y)) return KEY;
    if (edgeOf(inside, x, y)) return C(0x14501f);
    for (int i = 0; i < 3; i++) {
      float dx = x + 0.5f - (cs[i][0] - 2), dy = y + 0.5f - (cs[i][1] - 2), r = cs[i][2] * 0.5f;
      if (dx * dx + dy * dy <= r * r) return C(0x7be88a);
    }
    return y >= 14 ? C(0x1f7d2e) : C(0x33b04a);
  });
}
static Img makeRock() {
  const int w = 22, h = 14;
  auto inside = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= w || y >= h) return false;
    float a = (x + 0.5f - 11) / 10.8f, b = (y + 0.5f - 13.5f) / 13;
    return a * a + b * b <= 1;
  };
  return paint(w, h, [&](int x, int y) -> uint16_t {
    if (!inside(x, y)) return KEY;
    if (edgeOf(inside, x, y)) return C(0x3b3b4d);
    if ((x == 12 && y > 4 && y < 9) || (x == 13 && y >= 8 && y < 11)) return C(0x5d5d70);
    if (x + y < 15 && y < 8) return C(0xc9c9d9);
    if (x + y > 24) return C(0x7a7a8e);
    return C(0x9d9db0);
  });
}
static Img makeCrate() {
  const int w = 22, h = 22;
  return paint(w, h, [&](int x, int y) -> uint16_t {
    if (x == 0 || y == 0 || x == w - 1 || y == h - 1) return C(0x4e270b);
    if (x == 1 || y == 1 || x == w - 2 || y == h - 2) return C(0x8a5a22);
    if (x == 2 || y == 2) return C(0xefb868);
    if (x == y || x == w - 1 - y) return C(0x7a4a18);
    if (y == 7 || y == 14) return C(0xa8692a);
    return C(0xcf8a3a);
  });
}
static Img makePipe() {
  const int w = 26, h = 28;
  return paint(w, h, [&](int x, int y) -> uint16_t {
    bool rim = y < 8;
    int x0 = rim ? 0 : 2, x1 = rim ? w - 1 : w - 3;
    if (x < x0 || x > x1) return KEY;
    if (x == x0 || x == x1 || y == 0 || (y == 7 && rim) || y == h - 1) return C(0x0f4a1c);
    if (x == x0 + 1 || x == x0 + 2) return C(0x86f09a);
    if (x == x0 + 3) return C(0x47d062);
    if (x >= x1 - 4) return C(0x1a7a2e);
    return C(0x2fb54a);
  });
}
static Img makeBee(bool flap) {
  const int w = 22, h = 16;
  auto ell = [](int x, int y, float cx, float cy, float rx, float ry) {
    float a = (x + 0.5f - cx) / rx, b = (y + 0.5f - cy) / ry;
    return a * a + b * b <= 1;
  };
  auto body = [&](int x, int y) { return ell(x, y, 11.5f, 10.5f, 7.5f, 5) || ell(x, y, 5, 10, 4, 4); };
  auto wing = [&](int x, int y) {
    return flap ? (ell(x, y, 9, 6.5f, 4.5f, 2.2f) || ell(x, y, 14.5f, 6.5f, 4.5f, 2.2f))
                : (ell(x, y, 8.5f, 3.5f, 3, 3.3f) || ell(x, y, 14, 3.5f, 3, 3.3f));
  };
  return paint(w, h, [&](int x, int y) -> uint16_t {
    if (body(x, y)) {
      if (!body(x - 1, y) || !body(x + 1, y) || !body(x, y - 1) || !body(x, y + 1)) return C(0x2a1a0a);
      if (x == 3 && y == 9) return C(0x2a1a0a);  // olho
      if (x == 4 && y == 8) return C(0xffffff);
      if ((x >= 10 && x <= 11) || (x >= 14 && x <= 15)) return C(0x2a1a0a);  // listras
      return y <= 9 ? C(0xffe566) : C(0xffc21a);
    }
    if (y == 10 && x >= 19 && x <= 21) return C(0x2a1a0a);  // ferrão
    if (wing(x, y)) {
      bool we = !wing(x - 1, y) || !wing(x + 1, y) || !wing(x, y - 1) || !wing(x, y + 1);
      return we ? C(0x4f78b8) : C(0xffffff);
    }
    return KEY;
  });
}
static Img makeHeart(bool full) {
  static const char *rows[6] = {".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."};
  return paint(7, 6, [&](int x, int y) -> uint16_t {
    if (rows[y][x] != '#') return KEY;
    if (full) return (x == 1 && y == 1) ? C(0xffd0cc) : C(0xe8302a);
    return C(0x4a4f6e);
  });
}
static Img makeCloudLayer() {
  Img c = mk(W, 110);
  auto cloud = [&](float cx, float cy, float k) {
    float cs[5][3] = {{0, 0, 9 * k}, {-11 * k, 3 * k, 6 * k}, {11 * k, 3 * k, 7 * k}, {-20 * k, 7 * k, 4 * k}, {20 * k, 7 * k, 4 * k}};
    auto inside = [&](float px, float py) {
      for (int i = 0; i < 5; i++) {
        float dx = px + 0.5f - (cx + cs[i][0]), dy = py + 0.5f - (cy + cs[i][1]);
        if (dx * dx + dy * dy <= cs[i][2] * cs[i][2] && py <= cy + 9 * k) return true;
      }
      return false;
    };
    // laço com os mesmos limites (não inteiros) do original
    for (float y = cy - 12 * k; y <= cy + 10 * k; y++)
      for (float x = cx - 26 * k; x <= cx + 26 * k; x++) {
        // canvas fillRect(x, y, 1, 1) com coordenadas fracionárias: arredonda como o rasterizador
        int ix = (int)floorf(x + 0.5f), iy = (int)floorf(y + 0.5f);
        if (inside(x, y)) pix(c, ix, iy, (y >= cy + 5 * k) ? C(0xc4dcff) : C(0xffffff));
      }
  };
  cloud(70, 30, 1.3f);
  cloud(230, 62, 1);
  cloud(360, 24, 1.5f);
  cloud(445, 70, 0.9f);
  return c;
}
static Img makeHillLayer() {
  const int h = 92;
  Img c = mk(W, h);
  static const float hills[3][3] = {{90, 70, 44}, {270, 92, 66}, {415, 60, 34}};
  for (int x = 0; x < W; x++) {
    float top = h;
    for (int i = 0; i < 3; i++) {
      float t = (x - hills[i][0]) / hills[i][1];
      if (t > -1 && t < 1) {
        float v = h - hills[i][2] * sqrtf(1 - t * t);
        if (v < top) top = v;
      }
    }
    int itop = jsRound(top);
    for (int y = itop; y < h; y++) {
      uint16_t col = C(0x4cb34c);
      if (y == itop) col = C(0x2b7a2b);
      else if (((x + y * 2) % 23 == 0) || ((x + y * 2) % 23 == 1 && y > itop + 3)) col = C(0x78d478);
      else if (y > h - 10) col = C(0x3e9a3e);
      pix(c, x, y, col);
    }
  }
  return c;
}
// Céu do original: 7 faixas de cor (o resto é o fundo #5c94fc do canvas) + sol.
static const uint32_t SKY_BANDS[7] = {0x3f7cf0, 0x4a87f4, 0x5c94fc, 0x6fa3ff, 0x85b4ff, 0x9cc5ff, 0xb4d6ff};
static const int SKY_BH = (GY + 6) / 7;  // Math.ceil(GY / 7)
static uint16_t skyRow[SCR_H];
static Img makeSun() {  // a região do sol no makeSky (x 172..251, y 10..73)
  Img c = mk(80, 64);
  for (int y = 10; y < 74; y++)
    for (int x = 172; x < 252; x++) {
      float dx = x + 0.5f - 212, dy = y + 0.5f - 42, d = dx * dx + dy * dy;
      if (d <= 16 * 16)
        pix(c, x - 172, y - 10, d > 14 * 14 ? C(0xffb02e) : (d < 7 * 7 && dx < 0 && dy < 0 ? C(0xfff6a8) : C(0xffe14a)));
    }
  return c;
}
static Img makeGroundTile() {
  const int w = 32, h = SCR_H - GY + 2;  // original: H - GY + 2 (estendido até o fim da tela da placa)
  return paint(w, h, [&](int x, int y) -> uint16_t {
    if (y < 2) {
      if (y == 1 && ((x >= 5 && x <= 7) || (x >= 21 && x <= 24))) return C(0x3eb23f);
      if (y == 0 && (x == 6 || x == 22 || x == 23)) return C(0x3eb23f);
      return KEY;
    }
    if (y < 4) return C(0x8be04e);
    if (y < 8) return C(0x3eb23f);
    if (y == 8) return C(0x1f7a2e);
    int yy = y - 9, r = yy / 8;
    if (yy % 8 == 7) return C(0x7a3f12);
    if (((x + (r % 2 ? 8 : 0)) % 16) == 15) return C(0x7a3f12);
    if (yy % 8 == 0) return C(0xe8a050);
    return C(0xc97b2f);
  });
}
static Img makeButton(bool jump) {
  Img c = mk(24, 24);
  for (int y = 0; y < 24; y++)
    for (int x = 0; x < 24; x++) {
      float dx = x + 0.5f - 12, dy = y + 0.5f - 12, d = dx * dx + dy * dy;
      if (d > 11.5f * 11.5f) continue;
      if (jump) pix(c, x, y, d > 10.2f * 10.2f ? C(0x2a0a0a) : (dy > 5 ? C(0xb3241c) : ((dx + dy < -9) ? C(0xff8f80) : C(0xe8392f))));
      else pix(c, x, y, d > 10.2f * 10.2f ? C(0x0a1030) : (dy > 5 ? C(0x2a4fb0) : ((dx + dy < -9) ? C(0x8fb0ff) : C(0x3a6fe0))));
    }
  static const char *up[7] = {"....#....", "...###...", "..#####..", ".#######.", "...###...", "...###...", "...###..."};
  static const char *down[7] = {"...###...", "...###...", "...###...", ".#######.", "..#####..", "...###...", "....#...."};
  for (int y = 0; y < 7; y++)
    for (int x = 0; x < 9; x++)
      if ((jump ? up : down)[y][x] == '#') pix(c, 7 + x, (jump ? 6 : 7) + y, C(0xffffff));
  return c;
}

/* ================= ASSETS ================= */
static SpanImg clouds, hills;
static Img sky_sun, gtile, heartF, heartE, bee0, bee1, bush, rock, crate, pipeSpr, btnJump, btnDuck;

static const AtlasFrame *findFrame(const AtlasFrame *frames, size_t n, const char *name) {
  for (size_t i = 0; i < n; i++)
    if (strcmp(frames[i].name, name) == 0) return &frames[i];
  return nullptr;
}
static const AtlasFrame *frame(const char *name) {
  return findFrame(atlasFrames, sizeof(atlasFrames) / sizeof(atlasFrames[0]), name);
}
static const AtlasFrame *bframe(const char *name) {
  return findFrame(atlasBigFrames, sizeof(atlasBigFrames) / sizeof(atlasBigFrames[0]), name);
}

/* ================= SOUND ================= */
// Etapa 8: os sons do original (Web Audio) viram WAVs no cartão SD. Por enquanto, sem som.
enum class Sfx { Jump, Duck, Land, Hit, Start, GameOver };
static void sfx(Sfx) {}

/* ================= STATE ================= */
enum class St { Title, Play, Over };
static St state = St::Title;
static float stateT = 0;
static float dist = 0, speed = 70, playT = 0;
static int lives = MAX_LIVES;
static int best = 0;
static float jumpBuf = 0, walkClock = 0, shake = 0, clockT = 0;

enum class Ob { Bush, Rock, Crate, Pipe, Bee };
struct Obstacle {
  Ob t;
  const Img *spr;
  float x;
  int w, h, yb;
  bool hit;
  int idx;
};
static const int MAX_OBS = 8;
static Obstacle obstacles[MAX_OBS];
static int obsCount = 0;
static float nextGap = 0;
static int spawnCount = 0, beeStreak = 0;

enum class Ps { Run, Prep, Air, Land, Duck, Rise };
static struct {
  Ps s;
  float t, y, vy, inv;
} p;

static void resetGame() {
  dist = 0;
  playT = 0;
  speed = 150;
  lives = MAX_LIVES;
  obsCount = 0;
  spawnCount = 0;
  nextGap = 300;
  jumpBuf = 0;
  shake = 0;
  p.s = Ps::Run;
  p.t = 0;
  p.y = 0;
  p.vy = 0;
  p.inv = 0;
}
static int score() { return (int)floorf(dist / 10); }
static void goState(St s) {
  state = s;
  stateT = 0;
}

/* ================= INPUT ================= */
static bool duckHeld = false;
static void startRun() {
  resetGame();
  goState(St::Play);
  sfx(Sfx::Start);
}
static void pressJump() {
  if (state == St::Title) {
    if (stateT > 0.25f) startRun();
  } else if (state == St::Over) {
    if (stateT > 0.8f) startRun();
  } else {
    jumpBuf = 0.12f;
  }
}

/* ================= GAME LOGIC ================= */
static void spawnObstacle() {
  static const Ob ground[4] = {Ob::Bush, Ob::Rock, Ob::Crate, Ob::Pipe};
  Ob t;
  if (spawnCount == 2) t = Ob::Bee;
  else if (spawnCount > 2 && beeStreak < 2 && rnd(0, 1) < 0.32f) t = Ob::Bee;
  else t = ground[(int)floorf(rnd(0, 4)) & 3];
  beeStreak = (t == Ob::Bee) ? beeStreak + 1 : 0;
  const Img *spr = t == Ob::Bee ? &bee0 : t == Ob::Bush ? &bush : t == Ob::Rock ? &rock : t == Ob::Crate ? &crate : &pipeSpr;
  if (obsCount < MAX_OBS) obstacles[obsCount++] = {t, spr, (float)(W + 20), spr->w, spr->h, t == Ob::Bee ? 38 : 0, false, spawnCount};
  spawnCount++;
  nextGap = rnd(1.3f, 2.2f) * speed;
}

static void startPrep() {
  p.s = Ps::Prep;
  p.t = 0;
  jumpBuf = 0;
}
static void startDuck() {
  p.s = Ps::Duck;
  p.t = 0;
  sfx(Sfx::Duck);
}
static void updatePlayer(float dt) {
  p.t += dt;
  if (p.inv > 0) p.inv -= dt;
  switch (p.s) {
    case Ps::Run:
      if (jumpBuf > 0) startPrep();
      else if (duckHeld) startDuck();
      break;
    case Ps::Duck:
      if (jumpBuf > 0) startPrep();
      else if (!duckHeld) {
        p.s = Ps::Rise;
        p.t = 0;
      }
      break;
    case Ps::Rise:
      if (jumpBuf > 0) startPrep();
      else if (duckHeld) startDuck();
      else if (p.t >= 0.07f) {
        p.s = Ps::Run;
        p.t = 0;
      }
      break;
    case Ps::Prep:
      if (p.t >= PREP) {
        p.s = Ps::Air;
        p.t = 0;
        p.vy = V0;
        sfx(Sfx::Jump);
      }
      break;
    case Ps::Air:
      p.vy -= GRAV * dt;
      p.y += p.vy * dt;
      if (p.y <= 0 && p.vy < 0) {
        p.y = 0;
        p.vy = 0;
        p.s = Ps::Land;
        p.t = 0;
        sfx(Sfx::Land);
      }
      break;
    case Ps::Land:
      if (jumpBuf > 0) startPrep();
      else if (duckHeld && p.t > 0.08f) startDuck();
      else if (p.t >= LAND) {
        p.s = Ps::Run;
        p.t = 0;
      }
      break;
  }
}

static int hitHeight() { return (p.s == Ps::Duck && p.t > 0.06f) ? 27 : 42; }

static void saveBest() {
  Preferences prefs;
  prefs.begin("painel", false);
  prefs.putInt("game_best", best);
  prefs.end();
}

static void checkCollisions() {
  int left = PX - 9, right = PX + 9;
  if (p.inv > 0) return;
  for (int j = 0; j < obsCount; j++) {
    Obstacle &o = obstacles[j];
    if (o.hit) continue;
    if (right > o.x + 4 && left < o.x + o.w - 4 && p.y < o.yb + o.h - 4 && p.y + hitHeight() > o.yb + 2) {
      o.hit = true;
      lives--;
      p.inv = INV_TIME;
      shake = 0.25f;
      if (lives <= 0) {
        lives = 0;
        if (score() > best) {
          best = score();
          saveBest();
        }
        goState(St::Over);
        sfx(Sfx::GameOver);
      } else {
        sfx(Sfx::Hit);
      }
      return;
    }
  }
}

static void update(float dt) {
  clockT += dt;
  stateT += dt;
  if (jumpBuf > 0) jumpBuf -= dt;
  if (shake > 0) shake -= dt;

  if (state == St::Over) return;

  if (state == St::Title) speed = 110;
  else {
    playT += dt;
    speed = 150 + min(80.0f, playT * 1.6f);
  }
  dist += speed * dt;
  walkClock += dt * speed * 0.28f;

  if (state == St::Play) {
    updatePlayer(dt);
    for (int k = 0; k < obsCount; k++) obstacles[k].x -= speed * dt;
    while (obsCount && obstacles[0].x + obstacles[0].w < -20) {
      memmove(obstacles, obstacles + 1, (obsCount - 1) * sizeof(Obstacle));
      obsCount--;
    }
    Obstacle *last = obsCount ? &obstacles[obsCount - 1] : nullptr;
    if (!last) {
      if (spawnCount == 0 ? playT > 0.8f : true) spawnObstacle();
    } else if (last->x + last->w + nextGap < W + 20) {
      spawnObstacle();
    }
    checkCollisions();
  }
}

/* ================= RENDER ================= */
static const AtlasFrame *frameFor() {
  char name[8];
  if (state == St::Over) return frame("j0");
  if (state == St::Title || p.s == Ps::Run) {
    snprintf(name, sizeof(name), "w%d", (int)floorf(walkClock) % 24);
  } else if (p.s == Ps::Duck) {
    if (p.t < 0.06f) return frame("j1");
    if (p.t < 0.12f) return frame("j11");
    snprintf(name, sizeof(name), "c%d", (int)floorf((p.t - 0.12f) * 20) % 12);
  } else if (p.s == Ps::Rise) {
    return frame("j1");
  } else if (p.s == Ps::Prep) {
    snprintf(name, sizeof(name), "j%d", 1 + min(2, (int)floorf(p.t / (PREP / 3))));
  } else if (p.s == Ps::Air) {
    if (p.t < 0.07f && p.vy > V0 * 0.8f) return frame("j4");
    if (p.vy > V0 * 0.35f) return frame("j5");
    if (p.vy > -V0 * 0.25f) return frame("j6");
    return frame("j7");
  } else {
    snprintf(name, sizeof(name), "j%d", 8 + min(4, (int)floorf(p.t / (LAND / 5))));
  }
  return frame(name);
}

static void drawLayer(const SpanImg &img, int y, float factor) {
  int off = (int)floorf(fmodf(dist * factor, W));
  drawSpans(img, -off, y);
  drawSpans(img, W - off, y);
}

static void drawPlayer() {
  const AtlasFrame *f = frameFor();
  if (!f) return;
  if (p.inv > 0 && state == St::Play && (int)floorf(p.inv * 12) % 2 == 0) return;
  int dx = jsRound(PX - f->w / 2.0f), dy = jsRound(GY + 1 - p.y - f->h);
  int sw = max(8, 22 - (int)floorf(p.y / 5));  // sombra
  darkenRect(jsRound(PX - sw / 2.0f), GY + 1, sw, 2, 192);
  blit(atlasPixels, atlasW, f->x, 0, f->w, f->h, dx, dy);
}

static void drawHUD() {
  for (int i = 0; i < MAX_LIVES; i++) drawImg(i < lives ? heartF : heartE, 8 + i * 17, 8);
  char s[24];
  snprintf(s, sizeof(s), "PONTOS %d", score());
  drawText(s, W - 8 - textW(s, 2), 8, 2, C(0xffffff), true, C(0x1a2250));
}

static void panel(int x, int y, int w, int h) {
  fillRect(x, y, w, h, C(0xffffff));
  fillRect(x + 2, y + 2, w - 4, h - 4, C(0xffd23f));
  fillRect(x + 4, y + 4, w - 8, h - 8, C(0x1a2250));
}

/* ================= TITLE SCREEN ================= */
static const int TITLE_CX = 352, BIG_X = 120;
static void titleWord(const char *str, int cx, int y, int sc, uint16_t col, int delayBase) {
  int w = textW(str, sc), x0 = jsRound(cx - w / 2.0f);
  for (int i = 0; str[i]; i++) {
    float li = max(0.0f, min(1.0f, (stateT - (delayBase + i) * 0.07f) / 0.45f));
    int off = -jsRound((1 - li) * (1 - li) * 160);
    int dy = li >= 1 ? jsRound(sinf(clockT * 4 + (delayBase + i) * 0.7f) * 3) : 0;
    int x = x0 + i * 6 * sc;
    char ch[2] = {str[i], 0};
    drawText(ch, x + jsRound(sc * 0.6f), y + off + dy, sc, C(0x1a2250), true, C(0x1a2250));
    drawText(ch, x, y + off + dy, sc, col, false);
  }
}
static void star(int x, int y, int r, uint16_t col) {
  fillRect(x - r, y, 2 * r + 1, 1, col);
  fillRect(x, y - r, 1, 2 * r + 1, col);
  if (r > 1) fillRect(x, y, 1, 1, C(0xffffff));
}
static void drawTitleMaeva() {
  float t = fmodf(clockT, 3.2f);
  int yoff = 0;
  char name[8];
  if (t < 2.0f) {
    snprintf(name, sizeof(name), "w%d", ((int)floorf(walkClock / 2) % 12) * 2);
  } else {
    float u = t - 2.0f;
    const char *n;
    if (u < 0.10f) n = "j2";
    else if (u < 0.18f) n = "j3";
    else if (u < 0.86f) {
      float k = (u - 0.18f) / 0.68f;
      yoff = jsRound(46 * 4 * k * (1 - k));
      n = (k < 0.08f) ? "j4" : (k < 0.33f) ? "j5" : (k < 0.63f) ? "j6" : "j7";
    } else if (u < 0.94f) n = "j8";
    else if (u < 1.02f) n = "j9";
    else n = "j10";
    strcpy(name, n);
  }
  const AtlasFrame *f = bframe(name);
  if (!f) return;
  int sw = max(20, 56 - (int)floorf(yoff / 2.0f));
  darkenRect(jsRound(BIG_X - sw / 2.0f), GY + 1, sw, 3, 192);
  blit(atlasBigPixels, atlasBigW, f->x, 0, f->w, f->h, jsRound(BIG_X - f->w / 2.0f), GY + 1 - yoff - f->h);
  static const int sp[6][3] = {{-44, -92, 2}, {46, -84, 1}, {-58, -40, 1}, {60, -34, 2}, {-6, -140, 1}, {30, -128, 1}};
  for (int i = 0; i < 6; i++)
    if (sinf(clockT * 4 + i * 1.9f) > 0.15f) star(BIG_X + sp[i][0], GY + sp[i][1], sp[i][2], C(0xfff6a8));
}
static void drawTitle() {
  drawTitleMaeva();
  titleWord("CAPIT~", TITLE_CX, 46, 6, C(0xffe14a), 0);
  titleWord("MAEVA", TITLE_CX, 100, 7, C(0xffffff), 6);
  if (stateT > 1.0f) {
    if ((int)floorf(clockT * 2) % 2 == 0) {
      const char *m = "APERTE PARA JOGAR";
      drawText(m, jsRound(TITLE_CX - textW(m, 2) / 2.0f), 166, 2, C(0xffe14a), true, C(0x1a2250));
    }
    const char *h = "USE OS BOTOES PULAR E ABAIXAR";  // original: "BOTÃO 1 PULA   BOTÃO 2 ABAIXA"
    drawText(h, jsRound(TITLE_CX - textW(h, 1) / 2.0f), 192, 1, C(0xffffff), true, C(0x1a2250));
  }
}

/* ================= CONTROLES DA PLACA ================= */
// Botões 24x24 do original desenhados 4x (96 px) na faixa de terra; casinha "voltar" no topo (lugar do mudo).
static const int BTN_SCALE = 4, BTN_SIZE = 24 * BTN_SCALE, BTN_Y = SCR_H - BTN_SIZE - 4;
static const int BTN_JUMP_X = 12, BTN_DUCK_X = W - 12 - BTN_SIZE;
static const int BTN_HIT_R = BTN_SIZE / 2 + 10;  // tolerância para dedo pequeno
static const int HOME_X = 222, HOME_Y = 4, HOME_W = 36, HOME_H = 30;
static bool jumpDown = false, homeDown = false;

static void drawControls() {
  drawImg(btnJump, BTN_JUMP_X, BTN_Y);
  if (jumpDown) darkenRect(BTN_JUMP_X, BTN_Y, BTN_SIZE, BTN_SIZE, 205);
  drawImg(btnDuck, BTN_DUCK_X, BTN_Y);
  if (duckHeld) darkenRect(BTN_DUCK_X, BTN_Y, BTN_SIZE, BTN_SIZE, 205);
  drawText("PULAR", BTN_JUMP_X + BTN_SIZE + 8, BTN_Y + 40, 2, C(0xffffff), true, C(0x1a2250));
  drawText("ABAIXAR", BTN_DUCK_X - 8 - textW("ABAIXAR", 2), BTN_Y + 40, 2, C(0xffffff), true, C(0x1a2250));

  // Casinha: mesma moldura do botão de mudo do original
  blendRect(HOME_X, HOME_Y, HOME_W, HOME_H, C(0x0a1030), 141);
  fillRect(HOME_X, HOME_Y, HOME_W, 2, C(0xffffff));
  fillRect(HOME_X, HOME_Y + HOME_H - 2, HOME_W, 2, C(0xffffff));
  fillRect(HOME_X, HOME_Y, 2, HOME_H, C(0xffffff));
  fillRect(HOME_X + HOME_W - 2, HOME_Y, 2, HOME_H, C(0xffffff));
  static const char *house[10] = {"............", ".....##.....", "....####....", "...######...", "..########..",
                                  "...######...", "...##..##...", "...##..##...", "...######...", "............"};
  for (int r = 0; r < 10; r++)
    for (int c = 0; c < 12; c++)
      if (house[r][c] == '#') fillRect(HOME_X + 6 + c * 2, HOME_Y + 5 + r * 2, 2, 2, homeDown ? C(0xffe14a) : C(0xffffff));
}

static bool inCircle(int x, int y, int cx, int cy, int r) { return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r; }

// Lê o touch (até 2 dedos). Devolve false quando o dedo sai da casinha (voltar ao Painel).
static bool readControls() {
  int16_t xs[2], ys[2];
  uint8_t n = displayReadTouch(xs, ys, 2);
  bool jump = false, duck = false, home = false;
  for (uint8_t i = 0; i < n; i++) {
    jump |= inCircle(xs[i], ys[i], BTN_JUMP_X + BTN_SIZE / 2, BTN_Y + BTN_SIZE / 2, BTN_HIT_R);
    duck |= inCircle(xs[i], ys[i], BTN_DUCK_X + BTN_SIZE / 2, BTN_Y + BTN_SIZE / 2, BTN_HIT_R);
    home |= xs[i] >= HOME_X - 12 && xs[i] < HOME_X + HOME_W + 12 && ys[i] < HOME_Y + HOME_H + 14;
  }
  // Título e game over: tocar em qualquer lugar (menos na casinha) começa, como no original (o canvas inteiro
  // chama pressJump). Durante o jogo só os dois botões valem.
  static bool anyDown = false;
  bool any = n > 0 && !home;
  if (state != St::Play) {
    if (any && !anyDown) pressJump();
  } else if (jump && !jumpDown) {
    pressJump();
  }
  anyDown = any;
  jumpDown = jump;
  duckHeld = duck;
  // Sai ao soltar (não ao apertar): assim o LVGL não recebe o toque na tela inicial
  bool exit = homeDown && n == 0;
  homeDown = home || (homeDown && n > 0);
  return !exit;
}

static void render() {
  tx = shake > 0 ? ((int)floorf(clockT * 60) % 2 ? 1 : -1) : 0;
  // céu: preenche a largura toda (o original desloca a imagem do céu junto na tremida)
  for (int y = bandY0; y < bandY0 + BAND_H; y++) {
    uint32_t c2 = skyRow[y] * 0x10001u, *row = (uint32_t *)(band + (y - bandY0) * W);
    for (int x = 0; x < W / 2; x++) row[x] = c2;
  }
  drawImg(sky_sun, 172, 10);
  drawLayer(clouds, 8, 0.08f);
  drawLayer(hills, GY - 92, 0.22f);
  int off = (int)floorf(fmodf(dist, 32));
  for (int x = -off; x < W; x += 32) drawImg(gtile, x, GY - 2);
  for (int j = 0; j < obsCount; j++) {
    const Obstacle &o = obstacles[j];
    if (o.t == Ob::Bee) {
      int bob = jsRound(sinf(clockT * 6 + o.idx) * 2);
      drawImg((int)floorf(clockT * 12) % 2 ? bee1 : bee0, jsRound(o.x), GY + 2 - o.yb - o.h + bob);
    } else {
      drawImg(*o.spr, jsRound(o.x), GY + 2 - o.h);
    }
  }
  if (state != St::Title) drawPlayer();
  // dicas nos primeiros obstáculos
  if (state == St::Play && p.s != Ps::Air && p.s != Ps::Prep) {
    for (int j = 0; j < obsCount; j++) {
      const Obstacle &o = obstacles[j];
      if (o.idx < 4 && !o.hit && o.x - PX > 30 && o.x - PX < 170) {
        int hb = jsRound(sinf(clockT * 10) * 2);
        const char *msg = o.t == Ob::Bee ? "ABAIXE!" : "PULE!";
        drawText(msg, jsRound(PX - textW(msg, 2) / 2.0f), GY - 100 + hb, 2, C(0xffe14a), true, C(0x1a2250));
        break;
      }
    }
  }
  tx = 0;

  if (state == St::Title) {
    drawTitle();
  } else {
    drawHUD();
    if (state == St::Over) {
      darkenRect(0, 0, W, SCR_H, 141);  // rgba(0,0,0,0.45)
      panel(110, 58, 260, 136);
      char s[24];
      drawTextC("GAME OVER", 72, 4, C(0xff5a4a), C(0x000000));
      snprintf(s, sizeof(s), "PONTOS %d", score());
      drawTextC(s, 116, 2, C(0xffffff), C(0x000000));
      snprintf(s, sizeof(s), "RECORDE %d", best);
      drawTextC(s, 136, 2, C(0xffe14a), C(0x000000));
      if (stateT > 0.8f && (int)floorf(clockT * 2.5f) % 2 == 0) drawTextC("TENTAR DE NOVO", 166, 2, C(0x7be88a), C(0x000000));
    }
  }
  drawControls();
}

/* ================= LOOP ================= */
static bool running = false;
static uint32_t lastT = 0, fpsT = 0, fpsFrames = 0, fpsRenderUs = 0, fpsSendUs = 0;

void gameStart() {
  netPause();
  band = (uint16_t *)heap_caps_malloc(W * BAND_H * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  for (int y = 0; y < SCR_H; y++) skyRow[y] = C(y < 7 * SKY_BH ? SKY_BANDS[y / SKY_BH] : 0x5c94fc);
  sky_sun = makeSun();
  Img layer = makeCloudLayer();
  clouds = toSpans(layer);
  layer = makeHillLayer();
  hills = toSpans(layer);
  gtile = makeGroundTile();
  Img small = makeHeart(true);  // o original desenha os corações 7x6 em 14x12
  heartF = scaled(small, 2);
  freeImg(small);
  small = makeHeart(false);
  heartE = scaled(small, 2);
  freeImg(small);
  bee0 = makeBee(false);
  bee1 = makeBee(true);
  bush = makeBush();
  rock = makeRock();
  crate = makeCrate();
  pipeSpr = makePipe();
  small = makeButton(true);
  btnJump = scaled(small, BTN_SCALE);
  freeImg(small);
  small = makeButton(false);
  btnDuck = scaled(small, BTN_SCALE);
  freeImg(small);

  Preferences prefs;
  prefs.begin("painel", true);
  best = prefs.getInt("game_best", 0);
  prefs.end();

  resetGame();
  state = St::Title;
  stateT = clockT = walkClock = 0;
  jumpDown = duckHeld = homeDown = false;
  lastT = fpsT = millis();
  fpsFrames = fpsRenderUs = fpsSendUs = 0;
  running = true;
  Serial.printf("[jogo] iniciado | heap interno %u, maior bloco %u, psram %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

bool gameRunning() { return running; }

bool gameFrame() {
  uint32_t now = millis();
  float dt = (now - lastT) / 1000.0f;
  lastT = now;
  if (dt > 0.05f) dt = 0.05f;

  if (!readControls()) return false;
  update(dt);

  Arduino_GFX *gfx = displayGfx();
  for (bandY0 = 0; bandY0 < SCR_H; bandY0 += BAND_H) {
    uint32_t t0 = micros();
    render();
    // A tela recebe RGB565 big-endian: inverte os bytes aqui (32 bits por vez) para o barramento DMA
    // enviar o buffer direto, sem copiar nem inverter pixel a pixel
    uint32_t *w32 = (uint32_t *)band;
    for (int i = 0; i < W * BAND_H / 2; i++) {
      uint32_t v = w32[i];
      w32[i] = ((v & 0x00FF00FF) << 8) | ((v >> 8) & 0x00FF00FF);
    }
    uint32_t t1 = micros();
    gfx->draw16bitBeRGBBitmap(0, bandY0, band, W, BAND_H);
    fpsRenderUs += t1 - t0;
    fpsSendUs += micros() - t1;
  }

  fpsFrames++;
  if (now - fpsT >= 2000) {
    Serial.printf("[jogo] FPS %.1f (desenho %u ms + envio SPI %u ms por quadro)\n", fpsFrames * 1000.0f / (now - fpsT),
                  fpsRenderUs / 1000 / fpsFrames, fpsSendUs / 1000 / fpsFrames);
    fpsT = now;
    fpsFrames = fpsRenderUs = fpsSendUs = 0;
  }
  return true;
}

void gameStop() {
  running = false;
  freeSpans(clouds);
  freeSpans(hills);
  Img *imgs[] = {&sky_sun, &gtile, &heartF, &heartE, &bee0, &bee1, &bush, &rock, &crate, &pipeSpr, &btnJump, &btnDuck};
  for (Img *i : imgs) freeImg(*i);
  free(band);
  band = nullptr;
  netResume();
  Serial.println("[jogo] encerrado, memoria liberada");
}
