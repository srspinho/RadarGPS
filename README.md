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
(0 a 100). Se o som sair alto demais, ligue o pino GAIN do módulo ao VIN, o que
reduz o ganho para 6 dB.

### Testando o som sem o amplificador (`SOM_MODO 3`)

Nesse modo o ESP32-S3 gera o áudio em PDM (modulação de 1 bit) num único pino, o
GPIO6. Ele toca os mesmos chimes, com volume baixo:

- **Buzzer passivo ou disco piezo:** GPIO6 → resistor de 1 kΩ → buzzer → GND.
  Precisa ser buzzer *passivo* (o ativo só faz "biiip").
- **Fone de ouvido:** GPIO6 → resistor de 1 kΩ → capacitor de 100 nF → ponta do
  plugue P2; GND → corpo do plugue. Comece com o fone longe do ouvido.

Nunca ligue um alto-falante direto no pino: ele pede corrente demais e pode queimar o
GPIO. Depois do teste, volte para `SOM_MODO 2` e use o MAX98357A.

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
| Toque curto | liga/desliga o **mudo** |
| Toque longo (0,8 s) | alterna o **brilho** (100% / 50% / 15%) |

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
