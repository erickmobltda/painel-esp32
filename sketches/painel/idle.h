#pragma once

// Economia de energia na bateria (sem USB): escurece após 1 min sem toque e apaga após 5 min
// (tempos escolhidos pelo usuário). Com USB a tela fica sempre acesa; durante a partida do jogo também.
// Parada 10 min na bateria: desliga a placa pelo AXP2101 (religa com o PWR).
// Toque rápido no botão PWR apaga/acende a tela a qualquer momento (pressão longa desliga a placa, pelo AXP2101).
void idleBegin();
// Chamar a cada volta do loop(). Devolve false enquanto a tela estiver apagada (o loop não desenha).
bool idleLoop();
// Depuração (serial 'b'): age como se estivesse na bateria mesmo com o USB ligado.
void idleSimulateBattery(bool on);
// Caixa-preta de energia (/diag.txt na flash): grava um evento; despeja no serial ou apaga.
void idleDiag(const char *fmt, ...);
void idleDiagDump(bool clear);
