#!/usr/bin/env python3
"""Gera as palavras, imagens e vozes do app Monte a Palavra a partir de tools/monte_palavras.txt.

Só usa a biblioteca padrão do Python e programas que já vêm no Mac (say, afconvert). Uso:
    python3 tools/monte_assets.py

- Imagens: as mesmas PNGs de 128 px do Noto Emoji do app Ler (ver leitura_assets.py).
- Voz: `say -v Luciana` (pt-BR) → WAV 16 kHz mono (afconvert) em build/monte_voz/ (só na 1ª vez), sem os silêncios
  do começo e do fim, pico normalizado em -1 dBFS e comprimida em IMA ADPCM (4 bits por amostra).
Gera sketches/painel/monte_assets.h.
"""
import os
import subprocess
import sys
import unicodedata
import wave

sys.path.insert(0, os.path.dirname(__file__))
from leitura_assets import image  # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")
WORDS = os.path.join(ROOT, "tools", "monte_palavras.txt")
CACHE = os.path.join(ROOT, "build", "monte_voz")
OUT = os.path.join(ROOT, "sketches", "painel", "monte_assets.h")
VOICE = "Luciana"
RATE = 16000
SILENCE = 300          # |amostra| abaixo disso conta como silêncio (~-40 dBFS)
MARGIN = RATE * 30 // 1000  # 30 ms de folga antes e depois da fala
PEAK = int(32767 * 0.89)    # -1 dBFS

STEPS = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
         107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
         876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
         4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
         22385, 24623, 27086, 29794, 32767]
INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]


def slug(text):  # id ASCII minúsculo, sem acento (spec §6.5)
    return unicodedata.normalize("NFD", text).encode("ascii", "ignore").decode().lower()


def adpcm(samples):
    """IMA ADPCM, começando com preditor 0 e índice 0; 1º nibble no bit baixo. A placa decodifica igual."""
    pred, idx, out, low = 0, 0, bytearray(), None
    for s in samples:
        step, diff, code = STEPS[idx], s - pred, 0
        if diff < 0:
            code, diff = 8, -diff
        delta = step >> 3
        for bit, part in ((4, step), (2, step >> 1), (1, step >> 2)):
            if diff >= part:
                code |= bit
                diff -= part
                delta += part
        pred = max(-32768, min(32767, pred - delta if code & 8 else pred + delta))
        idx = max(0, min(88, idx + INDEX[code & 7]))
        if low is None:
            low = code
        else:
            out.append(low | code << 4)
            low = None
    if low is not None:
        out.append(low)
    return bytes(out)


def voice(word, sid):
    path = os.path.join(CACHE, sid + ".wav")
    if not os.path.exists(path):
        os.makedirs(CACHE, exist_ok=True)
        aiff = os.path.join(CACHE, sid + ".aiff")
        subprocess.run(["say", "-v", VOICE, "-o", aiff, word.lower()], check=True)
        subprocess.run(["afconvert", "-f", "WAVE", "-d", f"LEI16@{RATE}", "-c", "1", aiff, path], check=True)
        os.remove(aiff)
    with wave.open(path) as w:
        if (w.getframerate(), w.getnchannels(), w.getsampwidth()) != (RATE, 1, 2):
            sys.exit(f"{path}: esperava {RATE} Hz mono 16 bits")
        raw = w.readframes(w.getnframes())
    pcm = [int.from_bytes(raw[i:i + 2], "little", signed=True) for i in range(0, len(raw), 2)]
    loud = [i for i, s in enumerate(pcm) if abs(s) > SILENCE]
    pcm = pcm[max(0, loud[0] - MARGIN):loud[-1] + MARGIN]
    gain = PEAK / max(abs(s) for s in pcm)
    return adpcm([int(s * gain) for s in pcm]), len(pcm)


def main():
    words = []
    for line in open(WORDS, encoding="utf-8"):
        if line.strip() and not line.startswith("#"):
            levels, syl, code = line.split()
            lo, _, hi = levels.partition("-")
            words.append((int(lo), int(hi or lo), syl.split("-"), code))

    parts = ["// GERADO por tools/monte_assets.py a partir de tools/monte_palavras.txt. Não editar à mão.",
             "// Imagens: Noto Emoji (https://github.com/googlefonts/noto-emoji). Voz: macOS `say -v " + VOICE + "`.",
             "#pragma once", "#include <stdint.h>", "",
             f"#define MONTE_VOICE_RATE {RATE}  // voz: IMA ADPCM mono, preditor e índice iniciais 0", ""]
    img_total = voice_total = seconds = 0
    for i, (_, _, syl, code) in enumerate(words):
        text = "".join(syl)
        img = image(code)
        adp, n = voice(text, slug(text))
        img_total += len(img)
        voice_total += len(adp)
        seconds += n / RATE
        for name, data in (("IMG", img), ("VOZ", adp)):
            parts.append(f"static const uint8_t MONTE_{name}_{i}[] = {{  // {text}")
            parts += ["  " + ",".join(str(b) for b in data[j:j + 40]) + "," for j in range(0, len(data), 40)]
            parts.append("};")
        parts.append(f"static const uint32_t MONTE_VOZ_{i}_N = {n};  // amostras")

    parts += ["", "struct MonteWordData {", "  const char *id, *text, *syl;  // sílabas separadas por '|'",
              "  uint8_t minLevel, maxLevel;", "  const uint8_t *img;", "  uint32_t imgLen;",
              "  const uint8_t *voice;", "  uint32_t voiceSamples;", "};", "",
              "static const MonteWordData MONTE_WORDS[] = {"]
    for i, (lo, hi, syl, _) in enumerate(words):
        text = "".join(syl)
        parts.append(f'  {{"{slug(text)}", "{text}", "{"|".join(syl)}", {lo}, {hi}, MONTE_IMG_{i}, '
                     f"sizeof(MONTE_IMG_{i}), MONTE_VOZ_{i}, MONTE_VOZ_{i}_N}},")
    parts += ["};", f"static const int MONTE_WORD_COUNT = {len(words)};", ""]
    open(OUT, "w", encoding="utf-8").write("\n".join(parts))
    print(f"{len(words)} palavras, imagens {img_total // 1024} KB, voz {voice_total // 1024} KB "
          f"({seconds:.1f} s) -> {os.path.relpath(OUT)}")


if __name__ == "__main__":
    main()
