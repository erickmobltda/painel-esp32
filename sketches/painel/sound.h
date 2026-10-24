#pragma once

#include <stdint.h>

// Som do Painel: codec ES8311 + I2S + sintetizador (port do Web Audio da Capitã Maeva) numa tarefa
// no núcleo 0, que mistura tudo e alimenta o I2S. Quem chama só envia pedidos: nunca bloqueia.

enum class Snd {
  // efeitos do jogo (mesmos nomes do original: SND.jump(), SND.duck()...)
  Jump, Duck, Land, Hit, Start, GameOver,
  TitleMusic,  // SND.musicStart('title') — no original só tocava se o navegador permitisse autoplay
  StopMusic,   // SND.musicStop(true)
  Beep,        // bipe de teste do volume (tela ⚙)
  // app Ler
  Correct,     // acertou: arpejo alegre
  Wrong,       // errou: dois tons descendo, suaves (sem susto)
  LevelUp      // passou de nível: fanfarra
};

// Requer Wire.begin() e displayInit() antes (o amplificador é ligado pelo expansor TCA9554).
void soundBegin();
void soundPlay(Snd s);

// Volume do codec (0-100), salvo na NVS.
void soundSetVolume(int volume);
int soundVolume();

// Mudo do jogo (como o botão de mudo do original), salvo na NVS.
void soundSetMuted(bool muted);
bool soundMuted();
// O mudo acima é do jogo: o app Ler desliga essa regra enquanto está aberto (os sons dele são a resposta do acerto).
void soundIgnoreMute(bool ignore);

// Liga o som (I2S, mixer e amplificador) só no jogo e na tela ⚙: economiza ~12 KB de RAM interna e bateria.
void soundEnable(bool on);
