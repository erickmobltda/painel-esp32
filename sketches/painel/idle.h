#pragma once

// Economia de energia na bateria (sem USB): escurece após 1 min sem toque e apaga após 5 min
// (tempos escolhidos pelo usuário). Com USB a tela fica sempre acesa; durante a partida do jogo também.
void idleBegin();
// Chamar a cada volta do loop(). Devolve false enquanto a tela estiver apagada (o loop não desenha).
bool idleLoop();
// Depuração (serial 'b'): age como se estivesse na bateria mesmo com o USB ligado.
void idleSimulateBattery(bool on);
