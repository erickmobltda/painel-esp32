#include "sound.h"
#include "board.h"
#include "display.h"
#include "es8311.h"

#include <Arduino.h>
#include <ESP_I2S.h>
#include <Preferences.h>
#include <math.h>

// Port do createSound() do index.html da Capitã Maeva. O Web Audio agenda osciladores com rampas de
// volume/frequência; aqui cada "voz" faz o mesmo amostra a amostra, com o mesmo relógio (em amostras).

static const int SR = 48000;  // mesma configuração do exemplo oficial 04_es8311_example
static const int BLOCK = 256;
static const int MAX_VOICES = 32;

/* ================= AudioParam simplificado ================= */
enum : uint8_t { EV_SET, EV_LIN, EV_EXP };
struct Event {
  uint64_t at;  // amostra
  float value;
  uint8_t kind;
};
// setValueAtTime / linearRampToValueAtTime / exponentialRampToValueAtTime, avançando 1 amostra por chamada
struct Param {
  Event ev[4];
  uint8_t n = 0, i = 0;
  float v = 0, step = 0;
  bool expo = false;

  void add(uint64_t at, float value, uint8_t kind) { ev[n++] = {at, value, kind}; }
  void beginSegment() {
    v = ev[i].value;
    step = 0;
    expo = false;
    if (i + 1 < n && ev[i + 1].kind != EV_SET) {
      float len = (float)(ev[i + 1].at - ev[i].at);
      if (len < 1) len = 1;
      if (ev[i + 1].kind == EV_LIN) step = (ev[i + 1].value - v) / len;
      else {
        expo = true;
        step = powf(ev[i + 1].value / v, 1.0f / len);
      }
    }
  }
  float next(uint64_t s) {
    float out = v;
    if (i + 1 < n) {
      if (s + 1 >= ev[i + 1].at) {
        i++;
        beginSegment();
      } else if (expo) {
        v *= step;
      } else {
        v += step;
      }
    }
    return out;
  }
};

/* ================= Vozes ================= */
enum : uint8_t { W_SQUARE, W_TRIANGLE, W_SAWTOOTH, W_SINE, W_NOISE };
enum : uint8_t { BUS_SFX, BUS_MUSIC };
struct Voice {
  bool on;
  uint8_t wave, bus;
  uint32_t busGen;
  uint64_t start, stop;
  float phase;
  Param gain, freq;
  // ruído com filtro passa-altas (BiquadFilter highpass, Q padrão do Web Audio = 1 dB)
  uint32_t rng;
  float b0, b1, b2, a1, a2, x1, x2, y1, y2;
};
static Voice voices[MAX_VOICES];
static uint64_t now = 0;  // amostras já geradas (= ctx.currentTime * SR)

static uint64_t at(double t) { return (uint64_t)llround(t * SR); }
static double currentTime() { return (double)now / SR; }

static Voice *newVoice(uint8_t wave, uint8_t bus, double t, double stopT);

// Barramentos de música: musicStart cria um novo; musicStop(false) faz o antigo sumir (setTargetAtTime 0, 0.03 s)
// e o desconecta 300 ms depois; musicStop(true) desconecta na hora.
struct MusicBus {
  uint32_t gen = 0;
  bool alive = false;
  bool fading = false;
  double fadeStart = 0, disconnectAt = 0;
};
static MusicBus curBus, oldBus;
static uint32_t busGenCounter = 0;
static float busGain(uint8_t bus, uint32_t gen) {
  if (bus == BUS_SFX) return 1.0f;
  double t = currentTime();
  for (MusicBus *b : {&curBus, &oldBus}) {
    if (!b->alive || b->gen != gen) continue;
    if (!b->fading) return 0.8f;
    if (t >= b->disconnectAt) return 0;
    return 0.8f * expf(-(float)(t - b->fadeStart) / 0.03f);
  }
  return 0;  // barramento desconectado
}

static Voice *newVoice(uint8_t wave, uint8_t bus, double t, double stopT) {
  for (Voice &v : voices) {
    if (v.on) continue;
    memset(&v, 0, sizeof(v));
    v.on = true;
    v.wave = wave;
    v.bus = bus;
    v.busGen = curBus.gen;
    v.start = at(t);
    v.stop = at(stopT);
    return &v;
  }
  return nullptr;  // sem voz livre: o som é descartado (não trava)
}

static void startVoice(Voice *v) {
  v->gain.beginSegment();
  v->freq.beginSegment();
}

/* ================= primitivas do original ================= */
static void tone(uint8_t type, float f, double t, double dur, float vol, uint8_t bus) {
  Voice *v = newVoice(type, bus, t, t + dur + 0.02);
  if (!v) return;
  v->freq.add(at(t), f, EV_SET);
  v->gain.add(at(t), 0.0001f, EV_SET);
  v->gain.add(at(t + 0.008), vol, EV_LIN);
  v->gain.add(at(t + dur * 0.75), vol * 0.55f, EV_EXP);
  v->gain.add(at(t + dur), 0.0001f, EV_LIN);
  startVoice(v);
}
static void sweep(uint8_t type, float f0, float f1, double t, double dur, float vol) {
  Voice *v = newVoice(type, BUS_SFX, t, t + dur + 0.02);
  if (!v) return;
  v->freq.add(at(t), f0, EV_SET);
  v->freq.add(at(t + dur), f1, EV_EXP);
  v->gain.add(at(t), vol, EV_SET);
  v->gain.add(at(t + dur), 0.0001f, EV_EXP);
  startVoice(v);
}
static void noise(double t, double dur, float vol, float hp, uint8_t bus) {
  Voice *v = newVoice(W_NOISE, bus, t, t + dur + 0.02);
  if (!v) return;
  v->freq.add(at(t), 0, EV_SET);
  v->gain.add(at(t), vol, EV_SET);
  v->gain.add(at(t + dur), 0.0001f, EV_EXP);
  v->rng = 0x9E3779B9;  // mesma sequência sempre, como o buffer fixo de ruído do original
  float w0 = 2 * PI * hp / SR, cs = cosf(w0), alpha = sinf(w0) / (2 * powf(10, 1.0f / 20));
  float a0 = 1 + alpha;
  v->b0 = (1 + cs) / 2 / a0;
  v->b1 = -(1 + cs) / a0;
  v->b2 = (1 + cs) / 2 / a0;
  v->a1 = -2 * cs / a0;
  v->a2 = (1 - alpha) / a0;
  startVoice(v);
}
static void kick(double t, float vol) {
  Voice *v = newVoice(W_SINE, BUS_MUSIC, t, t + 0.14);
  if (!v) return;
  v->freq.add(at(t), 150, EV_SET);
  v->freq.add(at(t + 0.1), 42, EV_EXP);
  v->gain.add(at(t), vol, EV_SET);
  v->gain.add(at(t + 0.12), 0.0001f, EV_EXP);
  startVoice(v);
}
static void snare(double t, float vol) {
  noise(t, 0.09, vol, 1800, BUS_MUSIC);
  tone(W_TRIANGLE, 190, t, 0.06, vol * 0.8f, BUS_MUSIC);
}
static float mtof(int m) { return 440 * powf(2, (m - 69) / 12.0f); }

/* ================= músicas (notas copiadas do original) ================= */
struct Note {
  uint8_t midi, len;
};
// TITLE: calma e alegre, 132 BPM, C Am F G (durações em colcheias)
static const Note TITLE_LEAD[] = {
    {76, 1}, {79, 1}, {76, 1}, {72, 1}, {79, 2}, {76, 2}, {69, 1}, {72, 1}, {76, 1}, {72, 1}, {81, 2}, {76, 2},
    {77, 1}, {81, 1}, {77, 1}, {72, 1}, {77, 2}, {74, 2}, {71, 1}, {74, 1}, {79, 1}, {74, 1}, {71, 2}, {67, 2},
    {76, 1}, {79, 1}, {84, 1}, {79, 1}, {76, 2}, {72, 2}, {81, 1}, {79, 1}, {76, 1}, {72, 1}, {76, 2}, {69, 2},
    {77, 1}, {74, 1}, {72, 1}, {74, 1}, {77, 2}, {81, 2}, {79, 1}, {77, 1}, {76, 1}, {74, 1}, {72, 4}};
static const int TITLE_ROOTS[8] = {48, 45, 41, 43, 48, 45, 41, 43};
// GAME: rápida, 164 BPM, Sol maior, 16 compassos (durações em semicolcheias)
static const Note GAME_LEAD[] = {
    {79, 2}, {83, 2}, {86, 4}, {83, 2}, {79, 2}, {83, 4}, {81, 2}, {78, 2}, {81, 2}, {86, 2}, {81, 4}, {78, 4},
    {83, 2}, {79, 2}, {76, 2}, {79, 2}, {83, 4}, {88, 4}, {84, 2}, {79, 2}, {76, 2}, {79, 2}, {84, 6}, {0, 2},
    {79, 2}, {83, 2}, {86, 2}, {88, 2}, {86, 4}, {83, 2}, {79, 2}, {81, 2}, {86, 2}, {90, 2}, {86, 2}, {81, 4}, {78, 4},
    {84, 2}, {88, 2}, {84, 2}, {79, 2}, {84, 4}, {79, 2}, {76, 2}, {78, 2}, {81, 2}, {86, 4}, {81, 2}, {78, 2}, {74, 4},
    {88, 2}, {86, 2}, {83, 2}, {79, 2}, {83, 4}, {88, 4}, {84, 2}, {79, 2}, {84, 2}, {88, 2}, {84, 4}, {79, 4},
    {83, 2}, {86, 2}, {83, 2}, {79, 2}, {83, 2}, {86, 2}, {88, 4}, {86, 2}, {81, 2}, {78, 2}, {81, 2}, {86, 4}, {0, 2}, {78, 2},
    {76, 2}, {79, 2}, {83, 2}, {88, 2}, {86, 2}, {83, 2}, {79, 2}, {76, 2}, {72, 2}, {76, 2}, {79, 2}, {84, 2}, {79, 2}, {76, 2}, {72, 4},
    {74, 2}, {78, 2}, {81, 2}, {86, 2}, {81, 2}, {78, 2}, {74, 2}, {78, 2}, {79, 2}, {83, 2}, {86, 4}, {83, 2}, {79, 2}, {79, 4}};
enum Chord { G, D, Em, Cc };
static const Chord GAME_CHORDS[16] = {G, D, Em, Cc, G, D, Cc, D, Em, Cc, G, D, Em, Cc, D, G};
static const int GAME_ROOT[4] = {43, 50, 52, 48};
static const int GAME_ARP[4][4] = {{67, 71, 74, 71}, {66, 69, 74, 69}, {64, 67, 71, 67}, {64, 67, 72, 67}};
static const int GAME_BASS[8] = {0, 12, 0, 12, 0, 12, 7, 12};

// placeNotes: nota (ou 0) em cada passo da grade de semicolcheias
static Note TITLE_AT[128], GAME_AT[256];
static void placeNotes(const Note *lead, size_t count, int mult, Note *out, int steps) {
  memset(out, 0, steps * sizeof(Note));
  int n = 0;
  for (size_t i = 0; i < count && n < steps; i++) {
    out[n] = {lead[i].midi, (uint8_t)(lead[i].len * mult)};
    n += lead[i].len * mult;
  }
}

static void playTitle(int i, double t, double d) {
  if (i % 2) return;
  int e = i / 2, bar = e / 8, s = e % 8, root = TITLE_ROOTS[bar];
  double E = 2 * d;
  const Note &L = TITLE_AT[i];
  if (L.len && L.midi) tone(W_SQUARE, mtof(L.midi), t, L.len * d * 0.92, 0.045f, BUS_MUSIC);
  if (s == 0 || s == 4) tone(W_TRIANGLE, mtof(root), t, E * 1.8, 0.15f, BUS_MUSIC);
  else if (s == 2 || s == 6) tone(W_TRIANGLE, mtof(root + 7), t, E * 0.9, 0.11f, BUS_MUSIC);
  if (s % 2 == 1) noise(t, 0.035, 0.02f, 7000, BUS_MUSIC);
}
static void playGame(int i, double t, double d) {
  int bar = i / 16, s = i % 16;
  Chord ch = GAME_CHORDS[bar];
  const Note &L = GAME_AT[i];
  if (L.len && L.midi) tone(W_SQUARE, mtof(L.midi), t, L.len * d * 0.9, 0.04f, BUS_MUSIC);
  tone(W_SQUARE, mtof(GAME_ARP[ch][s % 4]), t, d * 0.8, 0.014f, BUS_MUSIC);
  if (s % 2 == 0) tone(W_TRIANGLE, mtof(GAME_ROOT[ch] + GAME_BASS[s / 2]), t, d * 1.8, 0.12f, BUS_MUSIC);
  if (s == 0 || s == 8 || s == 10) kick(t, 0.2f);
  if (s == 4 || s == 12) snare(t, 0.08f);
  if (s % 4 == 2) noise(t, 0.03, 0.02f, 7000, BUS_MUSIC);
  else if (s % 2 == 0) noise(t, 0.02, 0.01f, 8000, BUS_MUSIC);
}

struct Track {
  int bpm, steps;
  void (*play)(int, double, double);
};
static const Track TITLE = {132, 128, playTitle}, GAME = {164, 256, playGame};
static const Track *cur = &GAME;
static bool musicOn = false;
static int stepI = 0;
static double nextT = 0;

static void musicStop(bool immediately) {
  musicOn = false;
  if (!curBus.alive) return;
  if (immediately) {
    curBus.alive = false;
  } else {
    oldBus = curBus;
    oldBus.fading = true;
    oldBus.fadeStart = currentTime();
    oldBus.disconnectAt = currentTime() + 0.3;
    curBus.alive = false;
  }
}
static void musicStart(const Track *track, double delay) {
  cur = track;
  musicStop(true);
  curBus = {++busGenCounter, true, false, 0, 0};
  stepI = 0;
  nextT = currentTime() + delay;
  musicOn = true;
}
// O original agenda 0,4 s à frente num setInterval; aqui o próprio mixer agenda até o fim do bloco atual
static void schedUntil(double limit) {
  double d = 60.0 / cur->bpm / 4;
  while (nextT < limit) {
    cur->play(stepI, nextT, d);
    stepI = (stepI + 1) % cur->steps;
    nextT += d;
  }
}

/* ================= efeitos (api.* do original) ================= */
static void handle(Snd s) {
  double t = currentTime() + 0.005;
  switch (s) {
    case Snd::Jump: sweep(W_SQUARE, 320, 840, t, 0.17, 0.3f); break;
    case Snd::Duck: sweep(W_TRIANGLE, 260, 150, t, 0.09, 0.32f); break;
    case Snd::Land: sweep(W_TRIANGLE, 140, 70, t, 0.07, 0.24f); break;
    case Snd::Hit:
      sweep(W_SAWTOOTH, 430, 70, t, 0.34, 0.28f);
      sweep(W_SQUARE, 210, 60, t, 0.22, 0.12f);
      noise(t, 0.12, 0.22f, 1500, BUS_SFX);
      break;
    case Snd::Start: {
      static const int n[4] = {72, 76, 79, 84};
      for (int i = 0; i < 4; i++) tone(W_SQUARE, mtof(n[i]), t + i * 0.075, 0.09, 0.2f, BUS_SFX);
      musicStart(&GAME, 0.42);
      break;
    }
    case Snd::GameOver: {
      musicStop(false);
      double g = currentTime() + 0.05;
      tone(W_SQUARE, mtof(67), g, 0.17, 0.2f, BUS_SFX);
      tone(W_SQUARE, mtof(65), g + 0.18, 0.17, 0.2f, BUS_SFX);
      tone(W_SQUARE, mtof(64), g + 0.36, 0.17, 0.2f, BUS_SFX);
      sweep(W_SQUARE, mtof(60), mtof(55), g + 0.56, 0.6, 0.18f);
      break;
    }
    case Snd::TitleMusic: musicStart(&TITLE, 0.1); break;
    case Snd::StopMusic: musicStop(true); break;
    case Snd::Beep: tone(W_SQUARE, 880, t, 0.12, 0.25f, BUS_SFX); break;
  }
}

/* ================= mixer ================= */
static I2SClass i2s;
static QueueHandle_t queue;
static TaskHandle_t task = nullptr;
static volatile bool stopRequested = false;
static SemaphoreHandle_t stopped;
static volatile bool muted = false;
static int volume = 80;
static es8311_handle_t codec = nullptr;

static void renderBlock(int16_t *out) {
  static float mix[BLOCK];
  memset(mix, 0, sizeof(mix));
  uint64_t blockEnd = now + BLOCK;
  for (Voice &v : voices) {
    if (!v.on) continue;
    float bg = busGain(v.bus, v.busGen);
    if (bg == 0 || v.stop <= now) {
      v.on = false;  // terminou ou o barramento foi desconectado
      continue;
    }
    uint64_t s0 = max(v.start, now), s1 = min(v.stop, blockEnd);
    for (uint64_t s = s0; s < s1; s++) {
      float g = v.gain.next(s) * bg, f = v.freq.next(s), x;
      switch (v.wave) {
        case W_SQUARE: x = v.phase < 0.5f ? 1 : -1; break;
        case W_TRIANGLE: x = v.phase < 0.5f ? 4 * v.phase - 1 : 3 - 4 * v.phase; break;
        case W_SAWTOOTH: x = 2 * v.phase - 1; break;
        case W_SINE: x = sinf(2 * PI * v.phase); break;
        default: {  // ruído branco filtrado
          v.rng ^= v.rng << 13;
          v.rng ^= v.rng >> 17;
          v.rng ^= v.rng << 5;
          float in = (v.rng / 2147483648.0f) - 1;
          x = v.b0 * in + v.b1 * v.x1 + v.b2 * v.x2 - v.a1 * v.y1 - v.a2 * v.y2;
          v.x2 = v.x1;
          v.x1 = in;
          v.y2 = v.y1;
          v.y1 = x;
        }
      }
      v.phase += f / SR;
      if (v.phase >= 1) v.phase -= 1;
      mix[s - now] += x * g;
    }
  }
  float master = muted ? 0 : 1;
  for (int k = 0; k < BLOCK; k++) {
    float y = mix[k] * master;
    if (y > 1) y = 1;
    if (y < -1) y = -1;
    out[2 * k] = out[2 * k + 1] = (int16_t)(y * 32767);
  }
}

static void soundTask(void *) {
  static int16_t out[BLOCK * 2];
  while (!stopRequested) {
    Snd s;
    while (xQueueReceive(queue, &s, 0) == pdTRUE) handle(s);
    if (musicOn) schedUntil((double)(now + BLOCK) / SR);
    renderBlock(out);
    now += BLOCK;
    i2s.write((uint8_t *)out, sizeof(out));  // bloqueia até o DMA ter espaço: é o que dá o ritmo
  }
  xSemaphoreGive(stopped);
  vTaskDelete(nullptr);
}

void soundBegin() {
  placeNotes(TITLE_LEAD, sizeof(TITLE_LEAD) / sizeof(Note), 2, TITLE_AT, 128);
  placeNotes(GAME_LEAD, sizeof(GAME_LEAD) / sizeof(Note), 1, GAME_AT, 256);

  Preferences prefs;
  prefs.begin("painel", true);
  volume = prefs.getInt("volume", 80);
  muted = prefs.getBool("game_mute", false);
  prefs.end();

  // Mesma sequência do exemplo oficial 04_es8311_example
  codec = es8311_create(I2C_NUM_0, ES8311_ADDRESS_0);
  const es8311_clock_config_t clk = {.mclk_inverted = false, .sclk_inverted = false, .mclk_from_mclk_pin = false,
                                     .mclk_frequency = 0, .sample_frequency = SR};
  if (!codec || es8311_init(codec, &clk, ES8311_RESOLUTION_32, ES8311_RESOLUTION_32) != ESP_OK) {
    Serial.println("[som] ES8311 nao respondeu: sem som");
    return;
  }
  es8311_voice_volume_set(codec, volume, nullptr);
  es8311_microphone_config(codec, false);
  queue = xQueueCreate(16, sizeof(Snd));
  stopped = xSemaphoreCreateBinary();
  Serial.printf("[som] codec ok, volume %d%s\n", volume, muted ? " (mudo no jogo)" : "");
}

// I2S + tarefa do mixer + amplificador só enquanto alguma tela precisa de som (~12 KB de RAM interna).
void soundEnable(bool on) {
  if (!codec || on == (task != nullptr)) return;
  if (on) {
    i2s.setPins(PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DOUT, -1, -1);
    if (!i2s.begin(I2S_MODE_STD, SR, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
      Serial.println("[som] falha ao iniciar o I2S: sem som");
      return;
    }
    xQueueReset(queue);
    stopRequested = false;
    xTaskCreatePinnedToCore(soundTask, "som", 4096, nullptr, 3, &task, 0);
    displaySetAmplifier(true);
  } else {
    displaySetAmplifier(false);
    stopRequested = true;
    xSemaphoreTake(stopped, portMAX_DELAY);  // a tarefa termina o bloco atual e sai
    task = nullptr;
    i2s.end();
    for (Voice &v : voices) v.on = false;
    musicOn = false;
    curBus.alive = oldBus.alive = false;
  }
}

void soundPlay(Snd s) {
  if (task) xQueueSend(queue, &s, 0);  // fila cheia: o som é descartado, o jogo segue
}

void soundSetVolume(int v) {
  volume = constrain(v, 0, 100);
  if (codec) es8311_voice_volume_set(codec, volume, nullptr);
  Preferences prefs;
  prefs.begin("painel", false);
  prefs.putInt("volume", volume);
  prefs.end();
}
int soundVolume() { return volume; }

void soundSetMuted(bool m) {
  muted = m;
  Preferences prefs;
  prefs.begin("painel", false);
  prefs.putBool("game_mute", m);
  prefs.end();
}
bool soundMuted() { return muted; }
