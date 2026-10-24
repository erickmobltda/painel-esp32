# Especificação: "Monte a Palavra"

> **Status no Painel:** proposta, **não implementada**; as decisões do §15 continuam pendentes. Trazida de
> `erickmobltda/my_readmes/specs/monte-a-palavra.md` em 2026-10-24, sem alterações no texto.

> Jogo de alfabetização para uma criança de **5 anos**, em **português do Brasil**, que **já conhece as letras** e **está aprendendo as sílabas**.
> Este documento existe para que **qualquer IA ou pessoa** consiga implementar o jogo **sem inventar regras**. Onde houver dúvida, a regra é: **pergunte ao responsável antes de decidir** (ver [§15 Decisões em aberto](#15-decisões-em-aberto-perguntar-antes-de-implementar)).

---

## 0. Instruções para a IA que vai implementar

1. **Leia o documento inteiro antes de escrever código.**
2. **Não assuma nada que não esteja escrito aqui.** Se algo faltar, pergunte. Isso vale principalmente para a plataforma, a pinagem do hardware e o conteúdo das palavras.
3. **Não invente pinagem de hardware.** Use apenas a documentação oficial do fabricante da placa.
4. **Não invente separação silábica.** Use só as palavras da lista aprovada ([§6](#6-conteúdo-palavras)). Palavra nova precisa ser aprovada pelo responsável.
5. **Funciona 100% offline.** Nenhuma chamada de rede em tempo de execução.
6. **Separe a lógica do jogo do hardware** ([§12](#12-arquitetura)). A lógica precisa ser testável no computador, sem a placa.
7. Antes de dizer que terminou, cumpra todos os **critérios de aceite** ([§14](#14-critérios-de-aceite-e-casos-de-teste)).

---

## 1. Objetivo pedagógico

Fazer a ponte entre **sílaba** e **palavra**:
- a criança vê uma **imagem** (ex.: uma bola);
- monta a palavra com **sílabas** (BO + LA);
- ouve as sílabas **se juntando** na palavra: "BO… LA… **BOLA!**".

Princípios (obrigatórios):
- **Nunca punir.** Nada de som de "erro", contagem regressiva, vidas ou perder pontos.
- **Sem tempo limite.**
- **Sucesso frequente.** A dificuldade sobe devagar e só usa sílabas que a criança já conhece.
- **Sessões curtas**, de cerca de 5 palavras (~5 minutos).
- **Sempre dá para ouvir de novo:** a palavra, a sílaba, a instrução.

---

## 2. Glossário

| Termo | Significado |
|---|---|
| **Palavra-alvo** | A palavra que a criança precisa montar (ex.: BOLA). |
| **Sílaba-alvo** | Cada sílaba da palavra-alvo, em ordem (BO, LA). |
| **Distrator** | Sílaba extra que **não** faz parte da palavra (ex.: PA). |
| **Banco** | Área com as peças de sílabas embaralhadas, que a criança toca. |
| **Espaço** | Lugar vazio onde cada sílaba deve ficar. Há um espaço por sílaba-alvo. |
| **Peça** | Botão com o texto de uma sílaba. |
| **Rodada** | Uma palavra, do início até o acerto (ou até a solução automática). |
| **Sessão** | Sequência de rodadas, 5 por padrão. |
| **Sílabas liberadas** | Sílabas que o responsável marcou como "a criança já conhece". |

---

## 3. Tela (layout de referência)

Retrato (320×480 ou maior). As proporções valem para qualquer tela.

```
┌──────────────────────────────┐
│  ⭐⭐⭐☆☆        [🔊]  [⏸]   │  ← progresso da sessão / ouvir / pausa (pai)
│                              │
│        ┌──────────────┐      │
│        │              │      │
│        │   (imagem)   │      │  ← toque = fala a palavra
│        │     ⚽       │      │
│        └──────────────┘      │
│                              │
│     ┌──────┐  ┌──────┐       │
│     │  BO  │  │  __  │       │  ← ESPAÇOS (um por sílaba-alvo)
│     └──────┘  └──────┘       │
│                              │
│   ┌────┐   ┌────┐   ┌────┐   │
│   │ LA │   │ PA │   │    │   │  ← BANCO (peça usada fica "fantasma")
│   └────┘   └────┘   └────┘   │
└──────────────────────────────┘
```

Regras visuais:
- **Letras em CAIXA ALTA**, fonte sem serifa, sem itálico. A altura das letras na peça deve ser de pelo menos 1/12 da altura da tela.
- **Área de toque das peças:** no mínimo ~12 mm de cada lado e espaço de pelo menos ~3 mm entre peças. (Valores de referência para dedo de criança; ajustar no teste real.)
- **Cores:** todas as peças com a **mesma cor**. A cor **não pode dar pista** da resposta.
- **Peça usada:** quando vai para um espaço, ela some do banco e deixa um "fantasma" (contorno vazio) no lugar. Assim as outras peças não mudam de posição.
- **Espaços vazios** têm contorno tracejado. Espaços preenchidos mostram a peça.
- O ícone [⏸] só abre o menu do responsável com **toque longo de 3 segundos**, para a criança não sair do jogo sem querer.

---

## 4. Fluxo de uma rodada (máquina de estados)

```
INICIO_RODADA
  │  mostra imagem + espaços vazios + banco embaralhado
  │  toca áudio: "Vamos montar: BOLA."  (frase + palavra)
  ▼
AGUARDANDO_TOQUE ◄──────────────────────────────┐
  │                                              │
  ├─ toque numa peça do banco ──► vai pro 1º espaço vazio (da esquerda p/ direita)
  │                               toca o som da sílaba ("bó")
  │                               se todos os espaços estão cheios ─► VERIFICANDO
  │                               senão ───────────────────────────────┘
  ├─ toque numa peça num espaço ──► volta pro banco (lugar original) ──┘
  ├─ toque na imagem ou [🔊] ──► fala a palavra de novo ───────────────┘
  │
VERIFICANDO
  ├─ todas as sílabas certas na ordem certa ─► ACERTO
  └─ alguma errada ─► ERRO
ACERTO
  │  1. trava o toque
  │  2. destaca cada espaço em ordem, tocando a sílaba: "BO" … "LA"
  │  3. destaca a palavra toda e toca a palavra: "BOLA!"
  │  4. animação de comemoração (≤ 2 s) + frase de elogio aleatória
  │  5. registra o resultado (§9) ─► próxima rodada ou FIM_SESSAO
ERRO
  │  1. trava o toque por ~1 s
  │  2. peças erradas balançam de leve e voltam pro banco;
  │     peças certas na posição certa FICAM (ver §15, decisão D5)
  │  3. frase de incentivo: "Quase! Vamos tentar de novo."
  │  4. erros_na_rodada += 1 ; aplica dica (§5) ─► AGUARDANDO_TOQUE
```

Regras de comportamento:
- O **toque é bloqueado** enquanto um áudio de feedback ou uma animação toca, para evitar toques duplos. A exceção é o som de uma sílaba ao tocar uma peça: ele pode ser interrompido pelo próximo toque.
- **Verificação por texto, não pela peça.** Duas peças com o mesmo texto (ex.: BA‑NA‑**NA**) são intercambiáveis. Comparar `espaco[i].texto == alvo[i]`.
- O toque numa peça **coloca a peça no próximo espaço vazio**. O modo arrastar é **opcional** ([§15](#15-decisões-em-aberto-perguntar-antes-de-implementar), decisão D4). Tocar é mais fácil para uma criança de 5 anos.
- **Não verificar** enquanto houver espaço vazio.

---

## 5. Dicas progressivas (por rodada)

| Erros na rodada | O que acontece |
|---|---|
| 1 | Só a frase de incentivo. |
| 2 | **Dica sonora:** fala a palavra devagar, sílaba por sílaba ("BO… LA"). |
| 3 | **Dica visual:** a próxima peça correta pisca suavemente no banco. |
| 4 | **Solução guiada:** o jogo coloca as peças certas uma por uma, falando cada sílaba e depois a palavra. A rodada conta como "com ajuda" (§9), **nunca** como "erro", e não há som negativo. |

---

## 6. Conteúdo: palavras

### 6.1 Regras para escolher palavras
- **Substantivos concretos e fáceis de desenhar** (objetos, animais, comida).
- **Só sílabas liberadas.** Uma palavra só aparece se **todas** as sílabas dela estiverem liberadas.
- **A separação silábica precisa ser aprovada pelo responsável.** A IA **não deve** gerar palavras novas sozinha.
- **Níveis 1 a 3:** só sílabas simples consoante + vogal (CV). Ficam de fora:
  - dígrafos (CH, LH, NH, RR, SS, QU, GU);
  - sons nasais (ÃO, AN, EM, IN…);
  - encontros consonantais (BR, CL…);
  - sílabas terminando em consoante (MAR, SOL).
- **Evitar ambiguidade de som** nos níveis iniciais: C antes de E/I (CE, CI), G antes de E/I (GE, GI), S entre vogais (CASA = "caza"), e R inicial (RATO = "hato"). Esses casos ficam no nível 4 ou depois.
- **Acentos:** a sílaba é exibida e falada com acento (CA‑FÉ, BO‑NÉ). O nome do arquivo não leva acento (§7.3).

### 6.2 Níveis

| Nível | Sílabas por palavra | Distratores | Tipo de distrator |
|---|---|---|---|
| 1 | 2 | 1 | Bem diferente do alvo (outra consoante **e** outra vogal) |
| 2 | 2 | 2 | Qualquer sílaba liberada |
| 3 | 3 | 2 | Qualquer sílaba liberada |
| 4 | 2–3 | 2–3 | **Parecidos** (BA×PA, TA×DA, FA×VA, mesma consoante com outra vogal) |

Regra para subir de nível: **4 acertos sem ajuda nas últimas 5 rodadas** do nível atual. Regra para descer de nível: **3 rodadas seguidas resolvidas pela solução guiada**. Os números são configuráveis (§10).

### 6.3 Regras dos distratores
1. Vêm **só das sílabas liberadas**.
2. **Nunca** repetem uma sílaba-alvo.
3. **Nunca** permitem montar **outra palavra válida da lista** que caiba nos espaços. Exemplo: se o alvo é PATO e existe GATO na lista, não use GA como distrator no nível 1 ou 2. Isso só é permitido no nível 4.
4. Se não houver sílabas liberadas suficientes para os distratores, use **menos** distratores, mas no mínimo 0. Nunca use uma sílaba não liberada.

### 6.4 Embaralhamento
- Embaralhe as peças (alvos + distratores) com Fisher–Yates.
- Se a ordem embaralhada deixar as sílabas-alvo **já em sequência correta e adjacentes no início** (ex.: BO, LA, PA), embaralhe de novo. Limite de 10 tentativas; depois disso, aceite a ordem.

### 6.5 Lista inicial de exemplo (precisa ser aprovada)

Formato `palavras.json`:

```json
[
  { "id": "bola",    "palavra": "BOLA",    "silabas": ["BO", "LA"],         "nivel": 1, "imagem": "bola.png",    "audio": "bola.wav" },
  { "id": "pato",    "palavra": "PATO",    "silabas": ["PA", "TO"],         "nivel": 1, "imagem": "pato.png",    "audio": "pato.wav" },
  { "id": "boneca",  "palavra": "BONECA",  "silabas": ["BO", "NE", "CA"],   "nivel": 3, "imagem": "boneca.png",  "audio": "boneca.wav" },
  { "id": "banana",  "palavra": "BANANA",  "silabas": ["BA", "NA", "NA"],   "nivel": 3, "imagem": "banana.png",  "audio": "banana.wav" },
  { "id": "cafe",    "palavra": "CAFÉ",    "silabas": ["CA", "FÉ"],         "nivel": 4, "imagem": "cafe.png",    "audio": "cafe.wav" }
]
```

Campos obrigatórios: `id` (ASCII, minúsculo, sem acento), `palavra`, `silabas` (em ordem; `"".join(silabas) == palavra`), `nivel`, `imagem`, `audio`.
Validação ao carregar: se `join(silabas) != palavra`, a palavra é **rejeitada** e o problema registrado no log. Ela nunca é exibida.

Sugestões para o responsável revisar e aprovar:

| Nível | Palavras (separação silábica) |
|---|---|
| 1–2 | BO‑LA · PA‑TO · GA‑TO · DA‑DO · FA‑CA · VA‑CA · BO‑CA · MO‑TO · PI‑PA · BO‑TA · TA‑TU · LU‑VA · VE‑LA · DE‑DO · SA‑PO · SU‑CO · FO‑CA · LO‑BO · MA‑LA · CA‑MA |
| 3 | BO‑NE‑CA · CA‑VA‑LO · MA‑CA‑CO · SA‑PA‑TO · BA‑NA‑NA · TO‑MA‑TE · CA‑NE‑CA · JA‑NE‑LA · PE‑TE‑CA · PI‑PO‑CA · ME‑NI‑NA · MA‑LE‑TA |
| 4 | CA‑FÉ · BO‑NÉ · CA‑SA · RA‑TO · GE‑LO · DO‑CE |

> ⚠️ A palavra só entra no jogo se **todas** as sílabas dela estiverem na lista de sílabas liberadas **dessa criança**.

---

## 7. Áudio

### 7.1 Arquivos necessários
| Tipo | Pasta | Exemplo de nome | Conteúdo |
|---|---|---|---|
| Sílaba | `audio/silabas/` | `bo.wav`, `fe_agudo.wav` | Só o som da sílaba ("bó") |
| Palavra | `audio/palavras/` | `bola.wav` | Palavra inteira, falada naturalmente |
| Palavra devagar | `audio/palavras_devagar/` | `bola.wav` | "BO… LA" com pausa de ~0,5 s (dica do §5) |
| Frases | `audio/frases/` | `vamos_montar.wav`, `muito_bem_1.wav`… | Ver §7.2 |

### 7.2 Frases (texto exato)
- `vamos_montar.wav` → "Vamos montar:" (tocar antes da palavra)
- `muito_bem_1..4.wav` → "Muito bem!", "Isso aí!", "Você conseguiu!", "Parabéns!"
- `quase.wav` → "Quase! Vamos tentar de novo."
- `vou_ajudar.wav` → "Vou te ajudar."
- `fim_sessao.wav` → "Você montou todas as palavras de hoje!"

### 7.3 Nomes de arquivo
- Minúsculo, ASCII, sem acento nem espaço. Acentos viram sufixo: `fé` → `fe_agudo`, `é` → `e_agudo`, `ê` → `e_circunflexo`, `ã` → `a_til`.
- A conversão é feita por **uma única função** (`slug(texto)`), coberta por teste.

### 7.4 Formato
- WAV PCM 16 bits, **mono**, 16 kHz ou 22,05 kHz (o mesmo para todos os arquivos).
- Volume normalizado (~ -16 LUFS ou pico em -1 dBFS) para todos os arquivos terem o mesmo volume.
- Sem silêncio longo no início ou no fim: corte para menos de 100 ms.

### 7.5 Origem das vozes
Ver [§15](#15-decisões-em-aberto-perguntar-antes-de-implementar), decisão D3: gravar com a voz dos pais ou gerar no computador com voz sintética pt‑BR. **Sempre offline no aparelho:** os arquivos são gerados antes e copiados para ele.

### 7.6 Arquivo faltando
Se um áudio não existir, o jogo **não trava**. Ele registra no log e segue sem som. Ao carregar, uma rotina lista todos os arquivos faltando.

---

## 8. Imagens

- Pasta `imagens/`, nome igual ao `id` da palavra (`bola.png`).
- Quadradas e com fundo liso. Tamanho conforme a tela: ~200×200 px para 320×480; maior se a tela for maior.
- Estilo **ilustração simples e consistente** (mesmo traço, cores fortes). Um único objeto e **nenhum texto** na imagem.
- Licença: usar só imagens próprias, geradas para o projeto ou com licença livre. Registrar a origem em `imagens/CREDITOS.md`.
- Imagem faltando: mostrar um retângulo neutro e **pular a palavra** na seleção (§9.2), registrando no log.

---

## 9. Progresso e seleção de palavras

### 9.1 Dados salvos (`progresso.json`)
```json
{
  "nivel_atual": 1,
  "silabas_liberadas": ["BA","BE","BI","BO","BU","PA","PE","PI","PO","PU","LA","TO"],
  "palavras": {
    "bola": { "tentativas": 4, "acertos_sem_ajuda": 3, "com_ajuda": 1, "ultima_vez": "2026-10-24" }
  },
  "historico_nivel": [true, true, false, true]
}
```
- `acertos_sem_ajuda` conta acertos com 0 ou 1 erro e sem dica visual nem solução guiada.
- A gravação é **atômica**: escreve num arquivo temporário e depois renomeia. Assim, uma queda de energia não corrompe o progresso.
- Se o arquivo não existir ou estiver corrompido, o jogo cria um padrão e registra no log. Nunca trava.

### 9.2 Seleção da próxima palavra
1. Candidatas: palavras do `nivel_atual` com **todas** as sílabas liberadas, com imagem existente, e diferentes das **últimas 2 palavras** sorteadas.
2. Peso de cada uma: `1 + 2*com_ajuda + (3 se nunca vista)`, com teto de 6.
3. Sorteio ponderado.
4. Se não houver candidatas no nível, use o nível anterior. Se não houver em nenhum nível, mostre ao responsável a mensagem "Libere mais sílabas no menu" em vez de travar.

---

## 10. Configuração (`config.json`)

```json
{
  "palavras_por_sessao": 5,
  "subir_nivel_acertos": 4,
  "subir_nivel_janela": 5,
  "descer_nivel_guiadas_seguidas": 3,
  "manter_pecas_certas_no_erro": true,
  "modo_arrastar": false,
  "tocar_som_ao_tocar_peca": true,
  "volume": 70,
  "nome_crianca": null
}
```
Valor inválido ou ausente → usar o padrão acima e registrar no log.

---

## 11. Menu do responsável (toque longo de 3 s em ⏸)

- Marcar e desmarcar **sílabas liberadas**, em grade por família (BA BE BI BO BU…).
- Ver o progresso por palavra (tentativas, acertos e ajudas).
- Mudar o nível manualmente.
- Volume.
- Zerar o progresso, com confirmação dupla.
- Voltar ao jogo.

---

## 12. Arquitetura

Separar em **núcleo** (lógica pura, sem hardware) e **adaptadores** (hardware).

```
nucleo/
  modelo.py|.cpp        Palavra, Peça, Espaço, Rodada, Sessão
  regras.py             gerar_rodada(palavra, silabas_liberadas, nivel) -> pecas
                        colocar(peca), remover(espaco), verificar() -> ACERTO|ERRO
                        aplicar_dica(erros)
  selecao.py            proxima_palavra(progresso, lista, rng)
  progresso.py          carregar/salvar (atômico)
  slug.py               slug("FÉ") == "fe_agudo"
adaptadores/
  tela.*                desenhar estado, animações
  toque.*               eventos -> (x, y)
  audio.*               tocar(arquivo), parar(), esta_tocando()
  armazenamento.*       ler/escrever arquivos (SD ou disco)
app.*                   laço principal: eventos -> núcleo -> tela/áudio
testes/                 testes do núcleo (rodam no PC, sem hardware)
```

Regras:
- O **núcleo não importa nada de hardware** e recebe o gerador aleatório (`rng`) por parâmetro, para os testes serem determinísticos.
- O núcleo emite **eventos** (ex.: `TOCAR_AUDIO("silabas/bo.wav")`, `ANIMAR_ACERTO`). O app os repassa aos adaptadores.

---

## 13. Plataforma alvo

> **Ainda não decidida.** Ver [§15](#15-decisões-em-aberto-perguntar-antes-de-implementar), decisão D1. Não comece a parte de hardware sem essa resposta.

| | Opção A: Raspberry Pi 3B | Opção B: Waveshare ESP32‑Touch‑LCD‑3.5 |
|---|---|---|
| Linguagem | Python 3 | C++ (Arduino IDE) |
| Interface | pygame | LVGL |
| Tela | **A definir** (TFT do responsável; modelo ainda não informado) | 3,5" 320×480, ST7796 (SPI), toque FT6336 (I2C) |
| Áudio | Saída P2 do Pi ou USB (a definir) | Codec ES8311 (I2S) + alto-falante no conector MX1.25 |
| Arquivos | Cartão SD do sistema | Cartão TF da placa |
| Memória | 1 GB RAM | 520 KB SRAM + 2 MB PSRAM + 16 MB flash |
| Cuidados | Usar a opção Overlay File System do `raspi-config` (sistema somente leitura) para não corromper o cartão quando a energia acabar, com o progresso salvo numa partição gravável separada | Carregar **uma imagem por vez** (PSRAM). Pinagem **só** pela wiki oficial da Waveshare |

A lógica do núcleo é a mesma nas duas opções. Muda só a pasta `adaptadores/`.

---

## 14. Critérios de aceite e casos de teste

### 14.1 Testes automáticos do núcleo (obrigatórios)
1. `BOLA`: colocar BO e LA → `ACERTO`.
2. `BOLA`: colocar LA e BO → `ERRO`. Com `manter_pecas_certas_no_erro=true`, nenhuma peça fica (as duas estão na posição errada).
3. `BONECA`: colocar BO, CA, NE → `ERRO`. BO fica; CA e NE voltam ao banco.
4. `BANANA` (sílaba repetida): as duas peças NA são intercambiáveis e qualquer ordem entre elas resulta em `ACERTO`.
5. Distratores nunca repetem uma sílaba-alvo e nunca são sílabas não liberadas (testar 1.000 rodadas com `rng` fixo).
6. Distratores não formam outra palavra da lista nos níveis 1–3 (ex.: alvo PATO com GATO na lista → GA não aparece).
7. Liberadas = {BO, LA}, alvo BOLA no nível 2 → 0 distratores, sem erro.
8. Embaralhamento: as peças-alvo não ficam em ordem no início (com `rng` fixo em que isso ocorreria na primeira tentativa).
9. Dicas: 2 erros → evento de áudio "devagar"; 3 → evento piscar a próxima peça certa; 4 → solução guiada, registrada como `com_ajuda`.
10. Remover uma peça de um espaço devolve a peça ao **mesmo lugar** do banco.
11. Não verifica com espaço vazio.
12. `slug`: "BO"→"bo", "FÉ"→"fe_agudo", "BONÉ"→"bone_agudo"... (cobrir á é í ó ú â ê ô ã õ ç).
13. `palavras.json` com `silabas` que não formam a `palavra` → palavra rejeitada.
14. `progresso.json` corrompido → jogo inicia com o padrão.
15. Seleção: nunca repete as últimas 2 palavras quando há mais de 2 candidatas.
16. Subir e descer de nível conforme os parâmetros do §10.

### 14.2 Testes manuais no aparelho
- [ ] Rodada completa com acerto: ouvir "BO… LA… BOLA!" com os espaços destacados em ordem.
- [ ] Tocar rápido várias peças durante a animação não causa estado inválido.
- [ ] Tirar a energia no meio da sessão e religar: o progresso anterior está lá e o jogo abre.
- [ ] Remover um áudio do cartão: o jogo segue sem som e registra no log.
- [ ] Toque longo de 3 s abre o menu; um toque curto não abre.
- [ ] Uma criança de 5 anos consegue jogar uma sessão **sem ajuda de adulto** (teste real).

---

## 15. Decisões em aberto (perguntar antes de implementar)

| # | Pergunta | Opções | Status |
|---|---|---|---|
| D1 | Em qual plataforma rodar? | A) Raspberry Pi 3B (Python) · B) ESP32‑Touch‑LCD‑3.5 (C++/LVGL) | ❓ Pendente |
| D2 | Se for o Pi: qual tela, qual microfone/alto-falante, qual sistema/versão do Python? | — | ❓ Pendente |
| D3 | De onde vêm as vozes? | Gravadas pelos pais · Voz sintética pt‑BR gerada no PC · Misto (sintética + palavras da família gravadas) | ❓ Pendente (o responsável disse "tanto faz") |
| D4 | Ativar modo arrastar? | Só toque (padrão) · Toque + arrastar | ❓ Pendente |
| D5 | No erro, as peças certas ficam? | Sim (padrão) · Não, todas voltam | ❓ Pendente |
| D6 | Quais sílabas a criança já conhece hoje? | Lista do responsável | ❓ Pendente |
| D7 | A lista de palavras do §6.5 está aprovada? | Aprovar / editar | ❓ Pendente |
| D8 | Mostrar ou falar o nome da criança? | Sim (qual nome) / Não | ❓ Pendente |
| D9 | De onde vêm as imagens? | Desenhos próprios · Geradas por IA no PC · Banco com licença livre | ❓ Pendente |
| D10 | Integrar com reconhecimento de voz (Vosk) depois? | Fora do escopo desta versão | Registrado |

---

## 16. Fora do escopo desta versão
- Reconhecimento de voz da criança, que fica para outra fase com Vosk no Raspberry Pi.
- Internet, contas, nuvem.
- Sílabas complexas (dígrafos, nasais, encontros consonantais).
- Escrita livre ou teclado.
