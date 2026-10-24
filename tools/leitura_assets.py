#!/usr/bin/env python3
"""Gera as imagens e a lista de palavras do app Ler a partir de tools/leitura_palavras.txt.

Só usa a biblioteca padrão do Python. Uso:
    python3 tools/leitura_assets.py

Baixa as PNGs de 128 px do Noto Emoji (Google, licença Apache 2.0) para build/leitura_png/ (só na 1ª vez),
aplica fundo branco, converte para RGB565 e comprime com zlib (a placa descomprime com o tinfl da ROM).
Para cada palavra guarda as 4 mais parecidas do mesmo nível (menos letras diferentes): a placa sorteia 2
delas como opções erradas. Gera sketches/painel/leitura_assets.h.
"""
import os
import sys
import urllib.request
import zlib

sys.path.insert(0, os.path.dirname(__file__))
from game_assets import decode_png_rgba  # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")
WORDS = os.path.join(ROOT, "tools", "leitura_palavras.txt")
CACHE = os.path.join(ROOT, "build", "leitura_png")
OUT = os.path.join(ROOT, "sketches", "painel", "leitura_assets.h")
URL = "https://raw.githubusercontent.com/googlefonts/noto-emoji/main/2D/png/128/emoji_u{}.png"
SIZE = 128
NEAR = 4


def distance(a, b):
    row = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        prev, row[0] = row[:], i
        for j, cb in enumerate(b, 1):
            row[j] = min(prev[j] + 1, row[j - 1] + 1, prev[j - 1] + (ca != cb))
    return row[-1]


def image(code):
    path = os.path.join(CACHE, code + ".png")
    if not os.path.exists(path):
        os.makedirs(CACHE, exist_ok=True)
        urllib.request.urlretrieve(URL.format(code), path)
    w, h, px = decode_png_rgba(open(path, "rb").read())
    if (w, h) != (SIZE, SIZE):
        sys.exit(f"{code}: esperava {SIZE}x{SIZE}, veio {w}x{h}")
    out = bytearray()
    for i in range(0, len(px), 4):
        r, g, b, a = px[i:i + 4]
        r, g, b = ((c * a + 255 * (255 - a)) // 255 for c in (r, g, b))  # fundo branco (o cartão é branco)
        out += (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)).to_bytes(2, "little")
    return zlib.compress(bytes(out), 9)


def main():
    words = []
    for line in open(WORDS, encoding="utf-8"):
        if line.strip() and not line.startswith("#"):
            level, word, code = line.split()
            words.append((int(level), word, code))

    parts = ["// GERADO por tools/leitura_assets.py a partir de tools/leitura_palavras.txt. Não editar à mão.",
             "// Imagens: Noto Emoji (https://github.com/googlefonts/noto-emoji), Apache License 2.0.",
             "#pragma once", "#include <stdint.h>", "",
             f"#define LE_IMG_SIZE {SIZE}  // lado das imagens (RGB565 comprimido com zlib)",
             f"#define LE_NEAR {NEAR}       // palavras parecidas guardadas por palavra", ""]
    total = 0
    for i, (_, word, code) in enumerate(words):
        data = image(code)
        total += len(data)
        parts.append(f"static const uint8_t LE_IMG_{i}[] = {{  // {word}")
        parts += ["  " + ",".join(str(b) for b in data[j:j + 40]) + "," for j in range(0, len(data), 40)]
        parts.append("};")

    parts += ["", "struct LeWord {", "  uint8_t level;", "  const char *text;", "  const uint8_t *img;",
              "  uint32_t imgLen;", f"  uint8_t near[{NEAR}];  // índices em LE_WORDS", "};", "",
              "static const LeWord LE_WORDS[] = {"]
    for i, (level, word, _) in enumerate(words):
        same = [j for j, (lv, w, _) in enumerate(words) if lv == level and j != i]
        # menos letras diferentes primeiro; no empate, mesma letra inicial (mais difícil de chutar)
        same.sort(key=lambda j: (distance(word, words[j][1]), words[j][1][0] != word[0]))
        near = ",".join(str(j) for j in same[:NEAR])
        parts.append(f'  {{{level}, "{word}", LE_IMG_{i}, sizeof(LE_IMG_{i}), {{{near}}}}},')
    parts += ["};", f"static const int LE_WORD_COUNT = {len(words)};",
              f"static const int LE_LEVELS = {max(w[0] for w in words)};", ""]
    open(OUT, "w", encoding="utf-8").write("\n".join(parts))
    print(f"{len(words)} palavras, imagens {total // 1024} KB comprimidas -> {os.path.relpath(OUT)}")


if __name__ == "__main__":
    main()
