# App Pintar

Código: `paint.cpp`. Serial: `p` abre.

## O que a criança vê
- Área de desenho branca de 384×320 e uma barra de 96 px à direita com:
  casinha (volta ao soltar o dedo), lixeira ("Apagar tudo?" **Sim**/**Voltar**), **8 cores**, **glitter** (pontinhos
  de 6 cores brilhantes, parados), **borracha** (2× a espessura) e **3 espessuras** (raios 3, 7 e 14 px).
- Traço contínuo: carimbos interpolados entre as leituras do toque; falhas de leitura de até 60 ms não quebram o traço.

## Regras
- Desenha direto na tela, sem LVGL: só o retângulo alterado é enviado (bytes invertidos na RAM interna + DMA).
  O canvas RGB565 fica na PSRAM.
- Salva em `/desenho.bin` na LittleFS (RLE, pares quantidade+cor) ao sair e 15 s depois do último traço; reabre o
  último desenho.
- Abre por `paintRequestStart()` e inicia no `loop()` **depois** do `lv_timer_handler()` (ver LESSONS.md).
