# Painel

Firmware para a placa **Waveshare ESP32-Touch-LCD-3.5** (ESP32, tela de 3,5" sensível ao toque, alto-falante, bateria).
A tela inicial tem quatro apps, pensados para uma criança pequena:

| App | O que faz |
|---|---|
| **Músicas** | Mostra as capas de uma playlist do Spotify. Tocar numa capa começa a música no celular ou na Alexa (o som sai lá, não na placa). |
| **Jogo** | *Capitã Maeva*: corrida infinita com os botões **Pular** e **Abaixar**, música e efeitos pelo alto-falante. |
| **Pintar** | Tela branca com 8 cores, glitter, borracha, 3 espessuras e "apagar tudo". O desenho fica salvo. |
| **Ler** | Aparece uma palavra e 3 imagens; a criança lê e toca na imagem certa. 3 níveis, estrelas e sons de acerto e erro. |

No topo da tela inicial: Wi-Fi, ⚙ Configurações e bateria.

---

## Hardware

Placa **Waveshare ESP32-Touch-LCD-3.5** (não confundir com a ESP32-**S3**-Touch-LCD-3.5, que é outra placa e não roda
este firmware).

| Peça | O que é | Uso no Painel |
|---|---|---|
| Processador | ESP32-D0WDR2-V3, dual-core 240 MHz, Wi-Fi 2,4 GHz, Bluetooth 4.2 | tudo |
| Memória | 520 KB SRAM + 2 MB PSRAM + 16 MB flash | firmware 3 MB, arquivos (capas, desenho) 13 MB |
| Tela | 3,5" IPS 320×480, controlador ST7796S (SPI) | tela |
| Toque | FT6336, capacitivo (I2C) | toque |
| Áudio | codec ES8311 + conector MX1.25 para alto-falante | sons |
| Energia | AXP2101 + conector MX1.25 para bateria Li-ion 3,7 V | bateria, carga, botão PWR |
| Expansor | TCA9554 | reset da tela/toque, liga o amplificador |
| Outros | relógio PCF85063, slot microSD, microfone, USB-C | não usados |

Links oficiais da Waveshare:
- Página do produto: https://www.waveshare.com/esp32-touch-lcd-3.5.htm
- Wiki (especificações, bibliotecas, exemplos): https://www.waveshare.com/wiki/ESP32-Touch-LCD-3.5
- Documentação nova: https://docs.waveshare.com/ESP32-Touch-LCD-3.5
- Esquema elétrico (Rev 1.1): https://files.waveshare.com/wiki/ESP32-Touch-LCD-3.5/ESP32-Touch-LCD-3.5_Rev1.1.pdf
- Código de exemplo e bibliotecas: https://github.com/waveshareteam/ESP32-Touch-LCD-3.5

## Requisitos mínimos

**Para usar:**
- A placa acima. A bateria é opcional (a Waveshare vende com ou sem): sem ela, funciona só ligada no USB-C.
- Alto-falante no conector MX1.25, para ouvir os sons. Sem ele, o resto funciona.
- Wi-Fi **2,4 GHz**: a placa não enxerga redes de 5 GHz. O Jogo e o Pintar funcionam sem internet.
- Para o app Músicas: conta **Spotify Premium** (o Spotify só deixa controlar o que toca em contas Premium), o app do
  Spotify aberto no celular/computador ou uma Alexa ligada ao Spotify, e um app criado no painel de desenvolvedor do
  Spotify (passo a passo em [Configuração](#3-spotify-uma-vez-a-cada-6-meses)).

**Para gravar o firmware:**
- Cabo USB-C **de dados** (alguns cabos só carregam).
- Versão pronta: só o `esptool` ([Jeito rápido](#jeito-rápido-uma-versão-pronta-releases)).
- Compilar: Mac com Arduino IDE (traz o `arduino-cli` 1.5.1), `git` e ~7 GB livres para o core esp32 3.3.5 (o
  core traz compiladores para vários chips) e as bibliotecas. Testado só no macOS. Python 3 só para os scripts de `tools/` (o do Mac serve).

---

## Uso no dia a dia

### Botões da placa
Na lateral, da esquerda para a direita (está escrito na etiqueta de trás): **PWR · RST · BOOT**.

| Botão | Ação |
|---|---|
| **PWR**, toque rápido | Apaga/acende a tela. |
| **PWR**, segurar alguns segundos | Desliga a placa (feito pelo chip de energia, não pelo firmware). |
| **PWR**, com a placa desligada | Um toque rápido liga de novo. |
| **RST** | Reinicia a placa (como desligar e ligar). |
| **BOOT** | Só é usado para gravar firmware, se a gravação travar (ver abaixo). |

### Bateria
- **No USB**: a tela fica sempre acesa.
- **Na bateria**, sem ninguém mexer: escurece em **1 min**, apaga em **5 min** e **desliga sozinha em 10 min**.
  Tocar na tela acende enquanto ela só estiver escura ou apagada (o toque só acende, não aperta botão);
  depois de desligada, ligue com um toque no **PWR**.
- Durante uma partida do jogo a tela não escurece.
- A porcentagem da bateria pode ficar estranha nos primeiros usos: o chip de energia aprende a curva da bateria
  depois de uma carga e descarga completas.

### Volume
⚙ Configurações → arraste **Volume** e solte (toca um bipe de teste). No jogo, o alto-falante ao lado da casinha liga/desliga o som.
O mudo do jogo não vale para o Ler (lá o som faz parte da brincadeira).

### App Ler
- No alto aparece uma palavra em **MAIÚSCULAS**; embaixo, 3 imagens. A criança lê e toca na imagem da palavra.
- As imagens erradas são de palavras **parecidas** (BOLA → BOLO, BALA), para ela precisar ler a palavra toda.
- **Errou:** som curto e suave, a imagem fica apagada e ela tenta de novo, sem perder nada.
- **Acertou:** moldura verde e som de acerto. Se acertou **de primeira**, ganha uma estrela (as bolinhas no alto).
- **10 estrelas** = sobe de nível, com fanfarra. Nível 1: palavras de 2 sílabas simples (BOLA, GATO); nível 2:
  3 sílabas (SAPATO, BANANA); nível 3: sílabas mais difíceis (CHAVE, PALHAÇO, BORBOLETA). Sem tempo e sem vidas.
- **Para o adulto:** segure o dedo em **"Nível N"** (canto de cima à direita) para pular para o próximo nível; depois
  do último volta ao 1. O nível e as estrelas ficam salvos.
- Funciona sem internet. São 95 palavras; para mudar a lista, veja [docs/specs/apps/ler.md](docs/specs/apps/ler.md).

---

## Configuração (sem regravar o firmware)

### 1. Wi-Fi
Na primeira vez (ou em ⚙ → **Trocar Wi-Fi**) a placa cria a própria rede:
1. A tela mostra o nome da rede (**Painel-XXXX**) e uma senha de 8 dígitos.
2. No celular, entre nessa rede. A página de configuração abre sozinha; se não abrir, acesse **http://192.168.4.1**.
3. Escolha a rede da casa (precisa ser **2,4 GHz**), digite a senha e salve. A placa reinicia e conecta.

A placa lembra até **5 redes** (ex.: casa e casa da avó): ao ligar ou quando a conexão cai, ela procura as redes
por perto e conecta na salva de sinal mais forte. Cadastrar uma rede nova não apaga as outras; com mais de 5, a
mais antiga é esquecida. Cadastrar de novo uma rede já salva troca a senha dela.

### 2. Página de configuração da placa
Com o Wi-Fi conectado, abra ⚙ na placa: ela mostra o endereço (`http://192.168.x.x` ou `http://painel.local`) e um
**PIN de 6 dígitos**, que muda a cada vez que a placa liga. A página pede esse PIN para salvar qualquer coisa.

### 3. Spotify (uma vez a cada 6 meses)
**Uma vez só — criar o app no Spotify** (já feito):
1. https://developer.spotify.com/dashboard → **Create app**. Redirect URI: `http://127.0.0.1:8888/callback`
   (exatamente assim), API: **Web API**.
2. App → Settings → **User Management**: adicione nome e e-mail da conta Spotify que vai usar a placa.
3. Em **Basic Information** ficam o **Client ID** e o **Client Secret** (nunca coloque o secret no git ou em chat).

**Autorizar (ou renovar) — caminho 1, pela página da placa:**
1. Abra a página da placa no computador, preencha PIN, Client ID e Client Secret.
2. Clique em **"1. Abrir login do Spotify"**, entre na conta e clique em **Concordo**.
3. O navegador mostra um erro ("não foi possível conectar") — é esperado. Copie o endereço inteiro da barra
   (começa com `http://127.0.0.1:8888/callback?code=`), cole no campo da página e clique em **"2. Salvar"**.
   O código vale poucos minutos e uma vez só; se der erro, refaça a partir do passo 2.

**Autorizar (ou renovar) — caminho 2, pelo script no Mac:**
```bash
python3 tools/spotify_auth.py
```
O script pede Client ID e Secret, abre o login, recebe o código sozinho, testa a conta e envia para a placa
(pede o endereço e o PIN da tela ⚙). Só usa o Python que já vem no Mac.

**Validade:** a autorização vale **6 meses** (renovar não estende). A tela ⚙ mostra até quando vale e avisa em
vermelho quando faltarem menos de 15 dias. Se aparecer **"Reautorize o Spotify"**, repita um dos caminhos acima.

### 4. Playlist do app Músicas
Na página da placa, em **"Playlist do app Músicas"**: no Spotify, playlist → **Compartilhar → Copiar link**, cole
e salve. Regras do Spotify para apps pessoais:
- A playlist precisa ser **da mesma conta autorizada** na placa (de outras contas o Spotify só mostra nome e capa).
- A música toca num aparelho **dessa conta**:
  - se algo está tocando (ou pausado há pouco) num aparelho, toca nele;
  - se nada está ativo, toca no celular/computador com o app do Spotify aberto; sem nenhum, na Alexa vinculada à conta;
  - sem nenhum aparelho disponível, aparece o aviso "Nenhum aparelho".
- As capas ficam guardadas na placa (13 MB reservados: cabem milhares).

---

## Gravar o firmware

### Jeito rápido: uma versão pronta (releases)
Em https://github.com/erickmobltda/painel-esp32/releases baixe os 4 arquivos `.bin` da versão e rode
(troque a porta e a versão):
```bash
esptool --chip esp32 --port /dev/cu.usbmodemXXXX --baud 921600 write-flash \
  0x1000  painel-vX.Y-bootloader.bin \
  0x8000  painel-vX.Y-partitions.bin \
  0xe000  painel-vX.Y-boot_app0.bin \
  0x10000 painel-vX.Y-app.bin
```
Isso **não apaga** Wi-Fi, Spotify, desenho nem capas. A partir da v1.3 a área de arquivos é maior (13 MB); voltar
para a v1.2 ou anterior mantém Wi-Fi e Spotify, mas ⚠️ pode apagar capas e desenho (não testado). Para descobrir a porta: `ls /dev/cu.usbmodem*` com a placa
conectada por um cabo USB-C **de dados**. O `esptool` vem junto com o core do Arduino
(`arduino-data/packages/esp32/tools/esptool_py/5.1.0/esptool`).

### Compilando a partir do código
Pré-requisito: **Arduino IDE** instalado (o `arduino-cli` usado aqui vem dentro dele). Na pasta do projeto:
```bash
# uma vez: código oficial da Waveshare, bibliotecas nas versões testadas e o core esp32 3.3.5 (isolado do Arduino IDE)
git clone https://github.com/waveshareteam/ESP32-Touch-LCD-3.5 vendor/ESP32-Touch-LCD-3.5
git -C vendor/ESP32-Touch-LCD-3.5 checkout 19f266e8c257f3ed5519e2e4cc062d9e5a64fde4
for l in ESP32-audioI2S-master GFX_Library_for_Arduino lvgl OneButton SensorLib TCA9554 XPowersLib; do
  cp -R vendor/ESP32-Touch-LCD-3.5/examples/Arduino/libraries/$l arduino/libraries/; done
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
"$CLI" --config-file arduino-cli.yaml lib install ArduinoJson@7.4.3
"$CLI" --config-file arduino-cli.yaml core update-index
"$CLI" --config-file arduino-cli.yaml core install esp32:esp32@3.3.5

# compilar e gravar
FQBN="esp32:esp32:esp32:PSRAM=enabled,FlashSize=16M,PartitionScheme=huge_app"
PORT=/dev/cu.usbmodemXXXX
"$CLI" --config-file arduino-cli.yaml compile --fqbn $FQBN --build-path build/painel sketches/painel
"$CLI" --config-file arduino-cli.yaml upload -p $PORT --fqbn $FQBN --input-dir build/painel sketches/painel
```
Se a gravação parar em `Connecting...`, segure o botão **BOOT** enquanto ela tenta conectar.

> Atenção: não atualize o core para 3.3.6 ou mais novo — a biblioteca de tela da Waveshare (GFX 1.6.0) não compila
> nele. Detalhes em [LESSONS.md](LESSONS.md).

### Restaurar o firmware de fábrica da Waveshare
```bash
esptool --chip esp32 --port /dev/cu.usbmodemXXXX write-flash 0x0 vendor/ESP32-Touch-LCD-3.5/Firmware/01_factory.bin
```
O arquivo vem do repositório oficial (pasta `Firmware`). As configurações do Painel (Wi-Fi, Spotify) se perdem.

---

## Sons
Os sons do jogo e do Ler **não usam arquivos nem cartão SD**: o sintetizador do jogo original (Web Audio) foi portado
para a placa e gera música e efeitos ao vivo. O cartão SD não é usado pelo firmware.

---

## Quando algo dá errado

| Sintoma | O que fazer |
|---|---|
| "Sem Wi-Fi" / "sem conexão" | A placa tenta reconectar sozinha a cada 15 s. Se a rede mudou: ⚙ → Trocar Wi-Fi. |
| "Nenhum aparelho" no Músicas | Abra o Spotify no celular ou vincule a Alexa à mesma conta autorizada. |
| "Reautorize o Spotify" | A autorização venceu ou foi revogada: refaça a seção Spotify. |
| Capas aparecem como ♪ | Ainda baixando (primeira vez) ou acabou o espaço para capas. |
| Placa travou/reiniciou sozinha | Conecte no Mac e veja a "caixa-preta" (abaixo). |

**Caixa-preta e comandos de diagnóstico.** Com a placa no USB, abra o monitor serial (115200):
```bash
"$CLI" --config-file arduino-cli.yaml monitor -p $PORT -c baudrate=115200
```
Envie uma letra: **`i`** mostra a caixa-preta (ligou, escureceu, apagou, desligou, USB/bateria, com horário e
memória livre) e **`I`** apaga. Outras: `m` abre o Músicas, `g` o jogo, `p` o Pintar, `l` o Ler, `b` simula bateria (para testar
a economia de energia com o USB ligado). Se a placa travar, o ESP32 guarda um *coredump* na flash —
o passo a passo para lê-lo está em [LESSONS.md](LESSONS.md).

---

## Para quem for mexer no código
- [`PROJECT_CONTEXT.md`](PROJECT_CONTEXT.md): estado atual, decisões, versões fixadas, pinos, memória e como retomar.
- [`LESSONS.md`](LESSONS.md): problemas encontrados e como evitá-los.
- [`docs/specs/`](docs/specs/README.md): como funciona o núcleo e cada app (e propostas de apps novos, como o
  *Monte a Palavra*), com as regras para criar um app.
- `sketches/painel/`: o firmware (um arquivo por módulo: tela, energia, rede, Spotify, jogo, som, pintura...).
- `tools/spotify_auth.py`: autorização do Spotify pelo Mac. `tools/game_assets.py`: gera os sprites do jogo a partir
  do `index.html` original. `tools/leitura_assets.py`: gera as imagens e a lista de palavras do Ler.
- Imagens do Ler: [Noto Emoji](https://github.com/googlefonts/noto-emoji) do Google (licenças em [`licenses/`](licenses/README.md)).
