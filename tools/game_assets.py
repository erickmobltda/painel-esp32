#!/usr/bin/env python3
"""Converte os atlas de sprites embutidos no index.html da Capitã Maeva para C (RGB565).

Só usa a biblioteca padrão do Python. Uso:
    python3 tools/game_assets.py /caminho/para/capita_maeva_game/index.html

Gera sketches/painel/game_atlas.h. Os PNGs do jogo têm transparência "tudo ou nada"
(alfa 0 ou 255); pixels transparentes viram a cor-chave 0xF81F (magenta), que não aparece nos sprites.
"""
import base64
import json
import os
import re
import struct
import sys
import zlib

KEY = 0xF81F
OUT = os.path.join(os.path.dirname(__file__), "..", "sketches", "painel", "game_atlas.h")


def decode_png_rgba(data):
    """Decodifica PNG RGBA 8 bits sem entrelaçamento (o formato dos atlas do jogo)."""
    pos, idat = 8, b""
    while pos < len(data):
        length, = struct.unpack(">I", data[pos:pos + 4])
        kind, chunk = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            w, h, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
            if (depth, color, interlace) != (8, 6, 0):
                sys.exit(f"PNG não suportado: profundidade {depth}, tipo {color}, entrelaçado {interlace}")
        elif kind == b"IDAT":
            idat += chunk
    raw, bpp = zlib.decompress(idat), 4
    stride, out, prev, i = w * bpp, bytearray(), bytearray(w * bpp), 0
    for _ in range(h):
        filt, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            up = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if filt == 1:
                line[x] = (line[x] + a) & 255
            elif filt == 2:
                line[x] = (line[x] + up) & 255
            elif filt == 3:
                line[x] = (line[x] + ((a + up) >> 1)) & 255
            elif filt == 4:
                p = a + up - c
                pa, pb, pc = abs(p - a), abs(p - up), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else up if pb <= pc else c)) & 255
        out += line
        prev = line
    return w, h, out


def to_rgb565(px):
    pixels = []
    for i in range(0, len(px), 4):
        if px[i + 3] < 128:
            pixels.append(KEY)
        else:
            c = ((px[i] >> 3) << 11) | ((px[i + 1] >> 2) << 5) | (px[i + 2] >> 3)
            pixels.append(0xF81E if c == KEY else c)  # nunca colide com a cor-chave
    return pixels


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    html = open(sys.argv[1], encoding="utf-8").read()
    parts = ["// GERADO por tools/game_assets.py a partir do index.html da Capitã Maeva. Não editar à mão.",
             "#pragma once", "#include <stdint.h>", "",
             f"#define ATLAS_KEY 0x{KEY:04X}  // cor transparente", "",
             "struct AtlasFrame {", "  const char *name;", "  uint16_t x, w, h;", "};", ""]
    for var, meta_var, cname in (("ATLAS_B64", "ATLAS_META", "atlas"), ("ATLAS_BIG_B64", "ATLAS_BIG_META", "atlasBig")):
        png = base64.b64decode(re.search(var + r" = '([^']+)'", html).group(1))
        meta = json.loads(re.search(meta_var + r" = (\[.*?\]);", html).group(1))
        w, h, px = decode_png_rgba(png)
        pixels = to_rgb565(px)
        parts.append(f"static const uint16_t {cname}W = {w}, {cname}H = {h};")
        parts.append(f"static const AtlasFrame {cname}Frames[] = {{")
        parts += [f'  {{"{m[0]}", {m[1]}, {m[2]}, {m[3]}}},' for m in meta]
        parts.append("};")
        parts.append(f"static const uint16_t {cname}Pixels[{w * h}] = {{")
        for row in range(h):
            parts.append(",".join(f"0x{p:04X}" for p in pixels[row * w:(row + 1) * w]) + ",")
        parts.append("};")
        parts.append("")
        print(f"{cname}: {w}x{h}, {len(meta)} quadros")
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(parts))
    print(f"gerado {os.path.normpath(OUT)}")


if __name__ == "__main__":
    main()
