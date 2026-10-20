#pragma once

#include <Arduino.h>

enum class NetMode {
  Setup,      // ponto de acesso próprio para cadastrar o Wi-Fi
  Connected,
  Offline     // conectando, ou roteador fora do ar (continua tentando sozinho)
};

struct NetStatus {
  NetMode mode;
  String ssid;     // rede salva (ou do ponto de acesso, no modo Setup)
  String ip;
  String apPass;   // só no modo Setup
  String pin;      // PIN da página de configuração
  bool hasSavedWifi;
  bool spotifyConfigured;
  String spotifyAuthDate;
};

// Sem Wi-Fi salvo → modo Setup; senão conecta. Também sobe o servidor web (porta 80).
void netBegin();
void netLoop();
// Não apaga nada: só sobe o ponto de acesso para cadastrar outra rede (a antiga vale até salvar a nova).
void netStartSetup();
NetStatus netStatus();

// Desliga o Wi-Fi (jogo: economiza bateria e RAM) e religa depois. No modo Setup não faz nada.
void netPause();
void netResume();
