// =============================================================================
//  TesteAudio - bancada de testes do audio do RadarGPS (sem GPS, sem tela)
//
//  Controle pelo Monitor Serial (115200, "Nova linha" ou "Nenhum final de linha"):
//
//    m  troca o modo de saida:  I2S (MAX98357A) -> PDM (1 pino) -> PWM (onda quadrada)
//    1  seno 440 Hz            2  seno 1000 Hz          3  seno 250 Hz
//    4  varredura 150 Hz -> 5 kHz (ache a frequencia em que o som "grita")
//    5  onda quadrada 440 Hz (compare com o seno: e assim que soa um som "estridente")
//    i  chime de inicio        r  chime de radar        e  chime de excesso
//    +  volume +10             -  volume -10
//    s  silencio               ?  mostra este menu e a configuracao atual
//
//  Use o mesmo sintetizador.cpp do RadarGPS (copiado aqui). Se mudar as notas
//  la, copie o arquivo de novo para ca.
// =============================================================================
#include <ESP_I2S.h>
#include "sintetizador.h"

// --- Pinos (os mesmos do config.h do RadarGPS) ---
#define PINO_BCLK 4    // I2S: BCLK do MAX98357A   | PDM: sai o clock (nao precisa ligar)
#define PINO_LRC  5    // I2S: LRC do MAX98357A    | PDM: nao usado
#define PINO_DIN  6    // I2S: DIN do MAX98357A    | PDM e PWM: saida de audio

enum Modo : uint8_t { MODO_I2S, MODO_PDM, MODO_PWM, NUM_MODOS };
static const char *NOME_MODO[] = { "I2S padrao (MAX98357A)", "PDM 1 bit no PINO_DIN",
                                   "PWM onda quadrada no PINO_DIN (so tons)" };

enum Sinal : uint8_t { SINAL_SILENCIO, SINAL_SENO, SINAL_QUADRADA, SINAL_VARREDURA, SINAL_CHIME };

static I2SClass i2s;
static volatile Modo modoPedido = MODO_I2S;
static volatile Sinal sinal = SINAL_SILENCIO;
static volatile float freqHz = 440;
static volatile uint8_t volume = 40;
static volatile int somPedido = -1;   // chime a tocar (-1 = nenhum)
static Modo modoAtivo = NUM_MODOS;    // nenhum ainda

static void configurarModo(Modo m) {
  if (modoAtivo == MODO_I2S || modoAtivo == MODO_PDM) i2s.end();
  if (modoAtivo == MODO_PWM) ledcDetach(PINO_DIN);
  modoAtivo = m;

  bool ok = true;
  if (m == MODO_I2S) {
    i2s.setPins(PINO_BCLK, PINO_LRC, PINO_DIN);
    ok = i2s.begin(I2S_MODE_STD, SINT_TAXA_AMOSTRAGEM, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  } else if (m == MODO_PDM) {
    i2s.setPinsPdmTx(PINO_BCLK, PINO_DIN);
    ok = i2s.begin(I2S_MODE_PDM_TX, SINT_TAXA_AMOSTRAGEM, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  } else {
    ok = ledcAttach(PINO_DIN, 1000, 10);
    ledcWriteTone(PINO_DIN, 0);
  }
  Serial.printf("Modo: %s %s\n", NOME_MODO[m], ok ? "" : "-> FALHOU ao iniciar!");
}

// Gera um bloco de audio conforme o sinal escolhido
static void gerarBloco(int16_t *mono, int n) {
  static float fase = 0;
  static float freqVarredura = 150;
  const float amp = 0.5f * volume / 100.0f * 32767.0f;

  if (somPedido >= 0) {
    sintTocar((Som)somPedido);
    somPedido = -1;
  }
  switch (sinal) {
    case SINAL_CHIME:
      sintGerar(mono, n, volume / 100.0f);
      if (!sintTocando()) sinal = SINAL_SILENCIO;
      return;
    case SINAL_SILENCIO:
      for (int i = 0; i < n; i++) mono[i] = 0;
      return;
    default:
      break;
  }
  for (int i = 0; i < n; i++) {
    float f = freqHz;
    if (sinal == SINAL_VARREDURA) {
      freqVarredura *= 1.0000163f;  // ~5 s de 150 Hz a 5 kHz
      if (freqVarredura > 5000) freqVarredura = 150;
      f = freqVarredura;
    }
    fase += 2.0f * (float)M_PI * f / SINT_TAXA_AMOSTRAGEM;
    if (fase > 2.0f * (float)M_PI) fase -= 2.0f * (float)M_PI;
    float s = (sinal == SINAL_QUADRADA) ? (fase < (float)M_PI ? 1.0f : -1.0f) : sinf(fase);
    mono[i] = (int16_t)(s * amp);
  }
}

static void tarefaAudio(void *) {
  static int16_t mono[256];
  static int16_t estereo[512];
  for (;;) {
    if (modoPedido != modoAtivo) configurarModo(modoPedido);
    if (modoAtivo == MODO_PWM) {  // PWM: so liga/desliga um tom (sem volume)
      static float ultimo = -1;
      float f = (sinal == SINAL_SENO || sinal == SINAL_QUADRADA) ? freqHz : 0;
      if (f != ultimo) { ledcWriteTone(PINO_DIN, (uint32_t)f); ultimo = f; }
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    gerarBloco(mono, 256);
    for (int i = 0; i < 256; i++) estereo[2 * i] = estereo[2 * i + 1] = mono[i];
    i2s.write((uint8_t *)estereo, sizeof(estereo));
  }
}

static void mostrarMenu() {
  Serial.println();
  Serial.println("=== TesteAudio ===");
  Serial.printf("Modo: %s | volume %u%% | pinos BCLK=%d LRC=%d DIN=%d\n", NOME_MODO[modoPedido], volume,
                PINO_BCLK, PINO_LRC, PINO_DIN);
  Serial.println("m=modo  1=440Hz 2=1kHz 3=250Hz 4=varredura 5=quadrada 440Hz");
  Serial.println("i/r/e=chimes inicio/radar/excesso  +/-=volume  s=silencio  ?=menu");
}

void setup() {
  Serial.begin(115200);
  delay(1500);  // tempo para abrir o Monitor Serial
  xTaskCreatePinnedToCore(tarefaAudio, "audio", 4096, nullptr, 5, nullptr, 0);
  mostrarMenu();
}

void loop() {
  if (!Serial.available()) { delay(10); return; }
  char c = Serial.read();
  switch (c) {
    case 'm': modoPedido = (Modo)((modoPedido + 1) % NUM_MODOS); sinal = SINAL_SILENCIO; break;
    case '1': freqHz = 440;  sinal = SINAL_SENO; Serial.println("Seno 440 Hz"); break;
    case '2': freqHz = 1000; sinal = SINAL_SENO; Serial.println("Seno 1000 Hz"); break;
    case '3': freqHz = 250;  sinal = SINAL_SENO; Serial.println("Seno 250 Hz"); break;
    case '4': sinal = SINAL_VARREDURA; Serial.println("Varredura 150 Hz -> 5 kHz"); break;
    case '5': freqHz = 440; sinal = SINAL_QUADRADA; Serial.println("Quadrada 440 Hz"); break;
    case 'i': somPedido = SOM_INICIO;  sinal = SINAL_CHIME; Serial.println("Chime inicio"); break;
    case 'r': somPedido = SOM_RADAR;   sinal = SINAL_CHIME; Serial.println("Chime radar"); break;
    case 'e': somPedido = SOM_EXCESSO; sinal = SINAL_CHIME; Serial.println("Chime excesso"); break;
    case '+': volume = volume >= 90 ? 100 : volume + 10; Serial.printf("Volume %u%%\n", volume); break;
    case '-': volume = volume <= 10 ? 0 : volume - 10;   Serial.printf("Volume %u%%\n", volume); break;
    case 's': sinal = SINAL_SILENCIO; Serial.println("Silencio"); break;
    case '?': mostrarMenu(); break;
    default: break;  // ignora \r e \n
  }
}
