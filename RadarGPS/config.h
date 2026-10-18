// =============================================================================
//  RadarGPS - configuracoes
//  Ajuste aqui pinos, distancias e comportamento dos alertas.
// =============================================================================
#pragma once

// -----------------------------------------------------------------------------
//  Placa / tela
// -----------------------------------------------------------------------------
// 1 = Waveshare ESP32-S3-Touch-LCD-1.47 (JD9853 + touch AXS5106L)
// 0 = Waveshare ESP32-S3-LCD-1.47 (ST7789, sem touch)
#define PLACA_COM_TOUCH 1

#if PLACA_COM_TOUCH
// Pinos conferidos com o diagrama de pinagem da Waveshare (ESP32-S3-Touch-LCD-1.47).
// Obs.: GPIO13-18 sao do cartao SD e GPIO41/42 (I2C) sao compartilhados com o touch.
#define LCD_SCLK 38
#define LCD_MOSI 39
#define LCD_CS   21
#define LCD_DC   45
#define LCD_RST  40
#define LCD_BL   46

#define TP_SDA   42
#define TP_SCL   41
#define TP_RST   47
#define TP_INT   48
#define TP_ENDERECO_I2C 0x63   // AXS5106L
#else
// Pinos da ESP32-S3-LCD-1.47 (sem touch), conforme wiki da Waveshare
#define LCD_SCLK 40
#define LCD_MOSI 45
#define LCD_CS   42
#define LCD_DC   41
#define LCD_RST  39
#define LCD_BL   48
#endif

#define LCD_LARGURA 172
#define LCD_ALTURA  320
#define LCD_OFFSET_X 34    // o painel de 172 px fica centralizado na RAM de 240 px
#define LCD_OFFSET_Y 0
#define LCD_IPS      false // cores invertidas (fundo branco em vez de preto)? troque true/false

// Orientacao: 1 = horizontal, 3 = horizontal girada 180 graus (USB do outro lado)
#define LCD_ROTACAO  1

// Excesso de velocidade: 0 = pisca so uma moldura vermelha (fundo continua preto,
// melhor a noite); 1 = pisca a tela inteira em vermelho
#define LCD_ALERTA_TELA_CHEIA 0

// Imagem espelhada? 1 = inverte esquerda/direita (X) ou cima/baixo (Y).
// Texto ao contrario como num espelho -> LCD_ESPELHAR_X 1
// Texto de cabeca para baixo e espelhado -> LCD_ESPELHAR_Y 1 (com X 0)
// Tela girada 180 graus (de cabeca para baixo, mas legivel) -> X 1 e Y 1
#define LCD_ESPELHAR_X 1
#define LCD_ESPELHAR_Y 0

// Botao BOOT da placa (GPIO0): toque curto = mudo, toque longo = brilho.
// Funciona como alternativa ao touch.
#define PINO_BOTAO 0

// -----------------------------------------------------------------------------
//  Som
//  0 = sem som
//  1 = buzzer ATIVO 3.3V (bipes simples) no PINO_BUZZER
//  2 = chime sintetizado (sino suave) via modulo I2S MAX98357A + alto-falante
//      MAX98357A:  BCLK -> I2S_BCLK   LRC -> I2S_LRC   DIN -> I2S_DIN
//                  VIN -> 5V (VBUS) ou 3V3   GND -> GND   SD/GAIN: deixe soltos
//  3 = chime para TESTE SEM AMPLIFICADOR: o audio sai so no pino I2S_DIN (GPIO6)
//      GPIO6 -> resistor 1k -> buzzer PASSIVO (ou piezo) -> GND
//      Fone: GPIO6 -> 1k -> capacitor 100nF -> ponta do P2; GND -> corpo do P2
//      (volume baixo; nunca ligue alto-falante direto no pino)
// -----------------------------------------------------------------------------
#define SOM_MODO 2

#define I2S_BCLK 4
#define I2S_LRC  5
#define I2S_DIN  6
#define SOM_VOLUME 70        // 0 a 100

#define PINO_BUZZER 7        // so usado com SOM_MODO 1

// Enquanto estiver acima do limite, repete o aviso de excesso a cada X ms
#define SOM_REPETIR_EXCESSO_MS 1500

// -----------------------------------------------------------------------------
//  GPS Beitian BN-180
//  Ligacao:  BN-180 TX -> GPS_RX_PIN     BN-180 RX -> GPS_TX_PIN
//            BN-180 VCC -> 3V3 (ou 5V)   BN-180 GND -> GND
//
//  Usa os pinos GPIO1 e GPIO2 do conector lateral (livres: GPIO1 a GPIO11,
//  exceto GPIO3, que e pino de boot). Qualquer outro GPIO livre tambem serve.
//  So o fio TX do GPS e obrigatorio: sem o RX, use GPS_TX_PIN -1.
// -----------------------------------------------------------------------------
#define GPS_RX_PIN 1   // <- TX do BN-180
#define GPS_TX_PIN 2   // -> RX do BN-180
#define GPS_BAUD   9600   // padrao de fabrica do BN-180

// Fuso horario para o relogio da barra de status (Brasilia = -3)
#define FUSO_HORARIO -3

// -----------------------------------------------------------------------------
//  Deteccao de radares
// -----------------------------------------------------------------------------
// Distancia de alerta = velocidade * TEMPO_ANTECEDENCIA_S, limitada entre MIN e MAX.
// Ex.: a 60 km/h (16,7 m/s) * 25 s = 417 m; a 110 km/h = 764 m.
#define TEMPO_ANTECEDENCIA_S 25
#define DIST_ALERTA_MIN_M    300
#define DIST_ALERTA_MAX_M    1200

// O radar so conta como "a frente" se estiver dentro deste angulo em relacao
// ao rumo do veiculo (+/- graus). Evita alertas de radares que ficaram para tras.
#define ANGULO_CONE_GRAUS    35

// Abaixo desta velocidade o rumo do GPS nao e confiavel; usa o ultimo rumo valido.
#define VEL_MIN_RUMO_KMH     8

// Tolerancia para acusar EXCESSO: alerta quando velocidade > limite + tolerancia.
// 0 = avisa a partir de 1 km/h acima do limite (mais seguro).
#define TOLERANCIA_KMH       0

// Quantos km/h abaixo do limite o velocimetro ja fica amarelo
#define MARGEM_AMARELO_KMH   5

// Tipos que geram alerta (1 = sim, 0 = ignora)
#define ALERTA_RADAR_FIXO          1
#define ALERTA_SEMAFORO_RADAR      1
#define ALERTA_RADAR_MOVEL         1
#define ALERTA_SEMAFORO_CAMERA     1
#define ALERTA_POLICIA_RODOVIARIA  1
#define ALERTA_PEDAGIO             0
