# RadarGPS: alerta de radares com ESP32-S3 + GPS BN-180

O RadarGPS mostra no display da Waveshare ESP32-S3 1.47" a velocidade atual. Ele avisa
quando o veículo se aproxima de um radar, semáforo com câmera, posto da PRF ou pedágio
cadastrado em `dados/maparadar.csv`. Quando a velocidade passa do limite do radar à
frente, a tela pisca em vermelho e soa um aviso.

```
projetos/RadarGPS/
├── RadarGPS/              <- pasta do sketch (abra RadarGPS.ino na Arduino IDE)
│   ├── RadarGPS.ino       programa principal (GPS, botões, avisos)
│   ├── config.h           PINOS e AJUSTES; é o único arquivo que você precisa editar
│   ├── detector.h/.cpp    lógica de "radar à frente" e excesso de velocidade
│   ├── tela.h/.cpp        interface gráfica
│   ├── som.h/.cpp         saída de áudio (I2S ou buzzer)
│   ├── sintetizador.*     gerador dos chimes
│   ├── touch_axs5106l.*   leitura do touch
│   └── radares_db.h       base de radares (GERADA a partir do CSV)
├── dados/maparadar.csv    base original
└── ferramentas/
    ├── gerar_radares.py   CSV -> radares_db.h
    ├── teste_detector.cpp simulação do detector no PC
    └── gerar_previa_sons.cpp  gera .wav dos avisos sonoros
```

## Hardware

| Item | Observação |
|------|------------|
| Waveshare **ESP32-S3-Touch-LCD-1.47** | 172×320, driver JD9853, touch AXS5106L |
| GPS **Beitian BN-180** | u-blox M8, NMEA a 9600 baud (padrão de fábrica) |
| Módulo I2S **MAX98357A** + alto-falante 4–8 Ω, 2–3 W (recomendado) | chime sintetizado (`SOM_MODO 2`) |
| ou buzzer ativo 3,3 V | bipes simples (`SOM_MODO 1`, `PINO_BUZZER`) |

### Ligação do GPS

| BN-180 | ESP32-S3 |
|--------|----------|
| VCC (vermelho) | 3V3 (ou 5V) |
| GND (preto) | GND |
| **TX** | **GPIO 1** (`GPS_RX_PIN`) |
| **RX** | **GPIO 2** (`GPS_TX_PIN`, opcional) |

TX do GPS vai no RX do ESP e vice-versa. Os dois pinos ficam no conector lateral
esquerdo, logo abaixo do RST. Outros GPIOs livres do conector (4 a 11) também servem;
basta trocar `GPS_RX_PIN`/`GPS_TX_PIN` no `config.h`. Evite GPIO13 a GPIO18 (cartão
SD), GPIO41/42 (touch) e GPIO3 (pino de boot). Só o fio TX do GPS é obrigatório; sem
o RX, use `GPS_TX_PIN -1`.

Os pinos do display, do touch e do cartão SD foram conferidos com o diagrama de
pinagem oficial da Waveshare.

### Ligação do som (MAX98357A)

| MAX98357A | ESP32-S3 |
|-----------|----------|
| VIN | 5V/VBUS (mais volume) ou 3V3 |
| GND | GND |
| BCLK | **GPIO 4** (`I2S_BCLK`) |
| LRC | **GPIO 5** (`I2S_LRC`) |
| DIN | **GPIO 6** (`I2S_DIN`) |
| SD, GAIN | deixe desligados |

O alto-falante vai nos bornes + e − do módulo. O volume se ajusta em `SOM_VOLUME`
(0 a 100, padrão 40).

#### Som distorcido ("estourado")?

O software não satura em nenhum volume: o limitador arredonda os picos. Se o som
estourar, o problema é o amplificador entregando mais potência do que o alto-falante
ou a fonte aguentam. Em ordem de facilidade:

1. **Baixe o `SOM_VOLUME`**, por exemplo para 25.
2. **Reduza o ganho do MAX98357A pelo pino GAIN.** Solto, ele amplifica 9 dB.

   | Pino GAIN | Ganho |
   |-----------|-------|
   | resistor de 100 kΩ até o VIN | **3 dB** (o mais baixo) |
   | direto no VIN | 6 dB |
   | solto (padrão) | 9 dB |
   | direto no GND | 12 dB |
   | resistor de 100 kΩ até o GND | 15 dB |

3. **Alimente o módulo pelo VBUS (5 V)**, não pelo 3V3. Nos picos ele puxa centenas
   de mA, o regulador de 3,3 V da placa não segura e o som distorce. Um capacitor
   eletrolítico de 100 a 470 µF entre VIN e GND, perto do módulo, ajuda bastante.
4. **Use um alto-falante de 4–8 Ω e pelo menos 2 W.** Alto-falantes de 0,5 W
   (os de brinquedo) distorcem com qualquer volume razoável.
5. **Fios curtos** entre o ESP32 e o módulo nos sinais BCLK, LRC e DIN.

## Testando o som sem o amplificador (`SOM_MODO 3`)

Nesse modo o ESP32-S3 gera o áudio em PDM (modulação de 1 bit) num único pino, o
GPIO6. Ele toca os mesmos chimes, com volume baixo:

- **Buzzer passivo ou disco piezo:** GPIO6 → resistor de 1 kΩ → buzzer → GND.
  Precisa ser buzzer *passivo* (o ativo só faz "biiip").
- **Fone de ouvido:** GPIO6 → resistor de 1 kΩ → capacitor de 100 nF → ponta do
  plugue P2; GND → corpo do plugue. Comece com o fone longe do ouvido.

Nunca ligue um alto-falante direto no pino: ele pede corrente demais e pode queimar o
GPIO. Depois do teste, volte para `SOM_MODO 2` e use o MAX98357A.

### Bancada de testes do áudio (`testes/TesteAudio`)

Programa separado, sem GPS e sem tela, para testar só o som. Abra
`testes/TesteAudio/TesteAudio.ino`, grave e use o Monitor Serial a 115200, com
**USB CDC On Boot: Enabled**. Digite a tecla e aperte Enter; cada tecla recebida
aparece confirmada como `> tecla 'x'`.

| Tecla | Ação |
|-------|------|
| `a` / `b` / `c` | saída **I2S** (MAX98357A) / **PDM** (1 pino, para filtro RC + PAM8403) / **PWM** |
| `m` | passa para o próximo modo |
| `1` `2` `3` | seno de 440 Hz, 1 kHz e 250 Hz |
| `4` | varredura de 150 Hz a 5 kHz |
| `5` | onda quadrada de 440 Hz (para comparar com o seno) |
| `i` `r` `e` | chimes de início, radar e excesso |
| `+` `-` | volume |
| `s` / `?` | silêncio / menu |

Começa em **PDM no GPIO11**. Os pinos e o modo inicial ficam no topo do `.ino`
(`PINO_BCLK`, `PINO_LRC`, `PINO_DIN`, `MODO_INICIAL`).

#### PAM8403 (ou outro amplificador analógico) pelo PDM

O pino sai com 3,3 V, mas o PAM8403 espera algumas centenas de mV e já amplifica
~24 dB. Ligado direto, ou só com um RC simples, ele satura e o som fica estridente.
Use um filtro de dois estágios que também atenua o sinal:

```
GPIO ──[1kΩ]──┬──[10kΩ]──┬──[1µF]──► PAM8403 IN
              │          │
             10nF     1kΩ ∥ 10nF
              │          │
GND ──────────┴──────────┴─────────► PAM8403 GND
```

Alimente o PAM8403 pelo 5 V, com um eletrolítico de 220–470 µF perto dele, e use o
mesmo GND do ESP32.

## Preparando a Arduino IDE

1. **Arquivo → Preferências → URLs adicionais**:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
2. **Ferramentas → Placa → Gerenciador de placas**: instale **esp32** (Espressif), versão 3.x.
3. **Gerenciador de bibliotecas**: instale
   - **TinyGPSPlus** (Mikal Hart)
   - **GFX Library for Arduino** (moononournation), versão 1.4 ou mais nova
4. Configurações da placa (menu **Ferramentas**):
   - Placa: **ESP32S3 Dev Module**
   - USB CDC On Boot: **Enabled** (para ver o `Serial` pela USB)
   - Flash Size: o tamanho da sua placa (ex.: 16MB)
   - PSRAM: **OPI PSRAM** se a placa tiver (recomendado, o buffer da tela usa 110 KB)
   - Partition Scheme: **Default** já serve (a base ocupa ~200 KB)
5. Abra `RadarGPS/RadarGPS.ino` e carregue o sketch.

O Monitor Serial (115200) mostra a cada segundo a posição, a velocidade e o radar
detectado, o que ajuda nos testes.

## Como funciona

1. A base com **20.623 pontos** fica gravada na flash, ordenada por latitude. A cada
   posição nova do GPS (1×/s), uma **busca binária** encontra só os radares num raio
   de ~2 km. Não é preciso varrer a base inteira.
2. Para cada candidato, o detector calcula a **distância** e o **rumo** até o radar.
   O radar só conta se estiver **à frente**, dentro de ±35° do curso do veículo
   (`ANGULO_CONE_GRAUS`). Assim, radares que ficaram para trás ou em ruas
   perpendiculares não disparam alerta.
3. A distância de alerta acompanha a velocidade: `velocidade × 25 s`, entre 300 m e
   1.200 m. A 60 km/h o aviso começa a ~420 m; a 110 km/h, a ~760 m.
4. Com um radar "travado", o estado do alerta é um destes:
   - **APROXIMANDO** (amarelo): mostra a placa com o limite, o tipo, a distância e uma
     barra de progresso. O velocímetro fica amarelo a até 5 km/h do limite.
   - **EXCESSO** (moldura vermelha piscando + "REDUZA!" + bipes; com
     `LCD_ALERTA_TELA_CHEIA 1` a tela inteira pisca): velocidade > limite +
     `TOLERANCIA_KMH` (0 por padrão).
5. Quando o veículo passa pelo radar, o alerta é liberado. Esse radar e os vizinhos a
   até 80 m (por exemplo, o radar do outro sentido) ficam ignorados por 2 minutos.

Tipos sem limite cadastrado (limite 0 no CSV, como semáforo com câmera e a maioria dos
pedágios) mostram "!" na placa e nunca entram em EXCESSO. O pedágio vem
**desligado** por padrão (`ALERTA_PEDAGIO 0`).

### Avisos sonoros

Os sons são sintetizados em tempo real pelo ESP32 (`sintetizador.cpp`). Cada nota é
um sino: a nota base mais três harmônicos que decaem suavemente.

| Momento | Som |
|---------|-----|
| Ao ligar | duas notas subindo (Dó–Sol) |
| Radar à frente | "ding-dong-dong" descendente (Sol–Mi–Dó), estilo aeroporto |
| Acima do limite | três notas agudas rápidas, repetidas a cada 1,5 s (`SOM_REPETIR_EXCESSO_MS`) |

Para ouvir no PC antes de montar:

```bash
cd ferramentas
g++ -O2 -I../RadarGPS gerar_previa_sons.cpp ../RadarGPS/sintetizador.cpp -o previa && ./previa
```

Isso gera `som_inicio.wav`, `som_radar.wav` e `som_excesso.wav`. Para mudar as notas,
edite as tabelas `NOTAS_*` em `sintetizador.cpp`.

### Controles

| Ação | Toque na tela ou botão BOOT |
|------|-----------------------------|
| Toque curto | muda o **volume**: 20 → 40 → 60 → 80 → 100 → **MUDO** → 20… |
| Toque longo (0,8 s) | alterna o **brilho** (100% / 50% / 15%) |

A cada toque curto, um painel mostra o volume por 1,5 s e toca um "plim" no volume
novo. Na barra de status, um ícone de 5 barrinhas mostra o volume atual (ou "MUDO").
O volume e o brilho ficam gravados na memória e voltam assim ao religar. O
`SOM_VOLUME` do `config.h` só vale na primeira vez. Com buzzer ativo (`SOM_MODO 1`) não
existe volume: qualquer nível acima de zero toca igual.

## Atualizando a base de radares

Substitua `dados/maparadar.csv` e rode:

```bash
cd ferramentas
python3 gerar_radares.py ../dados/maparadar.csv ../RadarGPS/radares_db.h
```

## Testando a lógica no PC

```bash
cd ferramentas
g++ -O2 -I../RadarGPS teste_detector.cpp ../RadarGPS/detector.cpp -o teste && ./teste
```

O teste simula um carro passando por um radar real da base: acima do limite, dentro do
limite e já depois do radar.

## Limitações conhecidas

- **O CSV não informa o sentido do radar.** Uma rua paralela ou a pista contrária
  pode gerar alerta se o radar estiver dentro do cone à frente.
- A velocidade do GPS é a real. O velocímetro do carro costuma marcar 3 a 7% a mais.
- O BN-180 vem configurado para 1 atualização por segundo (a 100 km/h, ~28 m entre
  leituras). Isso basta para alertas a centenas de metros.
- **Tela:** o display é inicializado com a classe `Arduino_ST7789`, que usa os comandos
  padrão também aceitos pelo JD9853. Se a imagem aparecer espelhada, ajuste
  `LCD_ESPELHAR_X`/`LCD_ESPELHAR_Y`. Se ficar de cabeça para baixo na horizontal,
  use `LCD_ROTACAO 3`. Se o fundo aparecer branco (cores invertidas), troque
  `LCD_IPS` no `config.h`. Se aparecer deslocada, ajuste `LCD_OFFSET_X`. Se ficar em
  branco ou com ruído, compare com a sequência de inicialização do demo da Waveshare.
- O touch AXS5106L é usado apenas para detectar o toque. Se ele não for encontrado no
  I2C, o botão BOOT continua funcionando.
