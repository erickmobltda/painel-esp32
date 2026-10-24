# App "Ler" — plano (aprovado)

Decisões do usuário (2026-10-22): palavra em MAIÚSCULA de forma; 3 imagens por rodada; sem voz por enquanto;
filha já lê sílabas simples, mas sem 100% de facilidade; lista de palavras revisada por ele antes de implementar.

## Como funciona
- Tela: a palavra grande no topo; embaixo, 3 imagens lado a lado (posição sorteada).
- Imagens erradas = palavras **parecidas** do mesmo nível (escolhidas por script: menor número de letras diferentes,
  ex.: BOLA → BOLO, BALA). Assim não dá para acertar só pela primeira letra.
- Acertou: som de acerto (sintetizador do jogo), estrela, próxima palavra. Errou: som curto, a imagem errada fica cinza e
  ela tenta de novo (sem perder pontos). Estrela só conta se acertar na primeira tentativa.
- Sem cronômetro. Não repete a mesma palavra em seguida.
- Níveis: 10 estrelas → próximo nível (com festa). Nível e estrelas salvos na NVS. Dá para voltar de nível no ⚙ do app.
- Tela inicial ganha o 4º app ("Ler").

## Imagens
- Noto Emoji 2D do Google, PNG 128 px, licença Apache 2.0 (confirmado no README oficial). Arquivo LICENSE/atribuição no repo.
- Script `tools/leitura_assets.py` (só Python padrão, reaproveita o decodificador PNG de `game_assets.py`): baixa as PNGs,
  aplica fundo branco, converte para RGB565 e comprime com zlib → `leitura_assets.h` (~250 KB no firmware, cabe com folga).
- Na placa: descomprime com o `tinfl` da ROM do ESP32 para a PSRAM só as 3 imagens da rodada. Funciona sem internet.

## Fonte
- Montserrat 64 px só com A–Z e Á Â Ã Ç É Ê Í Ó Ô Õ Ú, gerada com `npx lv_font_conv@1.5.2` (o mesmo usado nas outras fontes).

## Etapas
- [x] 1. Usuário revisa a lista de palavras (níveis 1–3) e os grupos de parecidas
- [x] 2. Script de imagens + fonte; conferir tamanho do firmware
- [ ] 3. Tela do jogo (rodada, acerto/erro, sons) — testar na placa (implementado; falta conferência do usuário)
- [ ] 4. Níveis, estrelas, NVS, 4º botão na tela inicial — testar na placa (implementado; falta conferência do usuário)
- [x] 5. Docs (README, PROJECT_CONTEXT, LESSONS) + commit + release (v1.4, 2026-10-24)
