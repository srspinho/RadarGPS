#include "som.h"
#include "config.h"

#if SOM_MODO == 2 || SOM_MODO == 3
// -----------------------------------------------------------------------------
//  Chime sintetizado, enviado por I2S:
//   SOM_MODO 2: formato padrao I2S para um amplificador MAX98357A
//   SOM_MODO 3: modulacao PDM (1 bit) num unico pino (I2S_DIN), para testar com
//               buzzer passivo ou fone de ouvido, sem amplificador
//  Uma tarefa no nucleo 0 gera o audio continuamente (silencio quando nao ha
//  som), assim o loop principal, a tela e o GPS nunca ficam esperando.
// -----------------------------------------------------------------------------
#include <ESP_I2S.h>

static I2SClass i2s;
static QueueHandle_t filaSons;
static const uint8_t PEDIDO_PARAR = 0xFF;
static volatile uint8_t volumePorcento = SOM_VOLUME;

static void tarefaAudio(void *) {
  static int16_t mono[256];
  static int16_t estereo[512];
  for (;;) {
    uint8_t pedido;
    while (xQueueReceive(filaSons, &pedido, 0) == pdTRUE) {
      if (pedido == PEDIDO_PARAR) sintParar();
      else sintTocar((Som)pedido);
    }
    sintGerar(mono, 256, volumePorcento / 100.0f * SOM_LIMITE_SAIDA / 100.0f);
    for (int i = 0; i < 256; i++) estereo[2 * i] = estereo[2 * i + 1] = mono[i];
    i2s.write((uint8_t *)estereo, sizeof(estereo));  // bloqueia ate o DMA ter espaco
  }
}

void somIniciar() {
#if SOM_MODO == 2
  i2s.setPins(I2S_BCLK, I2S_LRC, I2S_DIN);
  bool ok = i2s.begin(I2S_MODE_STD, SINT_TAXA_AMOSTRAGEM, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
#else
  i2s.setPinsPdmTx(I2S_BCLK, I2S_DIN);  // o pino de clock nao precisa ser ligado
  bool ok = i2s.begin(I2S_MODE_PDM_TX, SINT_TAXA_AMOSTRAGEM, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
#endif
  if (!ok) {
    Serial.println("ERRO: falha ao iniciar o I2S (som)");
    return;
  }
  filaSons = xQueueCreate(8, sizeof(uint8_t));
  xTaskCreatePinnedToCore(tarefaAudio, "audio", 4096, nullptr, 5, nullptr, 0);
}

void somTocar(Som som) {
  uint8_t pedido = som;
  if (filaSons) xQueueSend(filaSons, &pedido, 0);
}

void somParar() {
  uint8_t pedido = PEDIDO_PARAR;
  if (filaSons) xQueueSend(filaSons, &pedido, 0);
}

void somVolume(uint8_t porcento) {
  volumePorcento = porcento > 100 ? 100 : porcento;
}

void somAtualizar(uint32_t) {}

#elif SOM_MODO == 1
// -----------------------------------------------------------------------------
//  Buzzer ativo: so liga/desliga, entao cada som vira uma sequencia de bipes.
// -----------------------------------------------------------------------------
static uint8_t bipsPendentes = 0;
static uint16_t intervaloMs = 250;
static uint32_t proximoBipMs = 0;
static uint32_t desligarMs = 0;

void somIniciar() {
  pinMode(PINO_BUZZER, OUTPUT);
  digitalWrite(PINO_BUZZER, LOW);
}

void somTocar(Som som) {
  bipsPendentes = (som == SOM_RADAR) ? 2 : 1;
  intervaloMs = 250;
}

void somParar() {
  bipsPendentes = 0;
  desligarMs = 0;
  digitalWrite(PINO_BUZZER, LOW);
}

void somVolume(uint8_t) {}  // buzzer ativo nao tem volume: so liga (>0) ou mudo (0)

void somAtualizar(uint32_t agora) {
  if (desligarMs && agora >= desligarMs) {
    digitalWrite(PINO_BUZZER, LOW);
    desligarMs = 0;
  }
  if (bipsPendentes && !desligarMs && agora >= proximoBipMs) {
    digitalWrite(PINO_BUZZER, HIGH);
    desligarMs = agora + 120;
    proximoBipMs = agora + intervaloMs;
    bipsPendentes--;
  }
}

#elif SOM_MODO == 4
// -----------------------------------------------------------------------------
//  Buzzer piezo PASSIVO: bipes curtos gerados por PWM (LEDC) no PINO_BUZZER.
//  O volume muda o ciclo de trabalho do PWM (50% = mais alto). Uma tarefa toca
//  os padroes com tempos exatos, sem depender do loop principal (que desenha
//  a tela) e sem trava-lo.
// -----------------------------------------------------------------------------
struct Bipe {
  uint16_t freqHz;   // 0 = pausa
  uint16_t duracaoMs;
};

// Piezos costumam soar mais alto entre 2 e 4 kHz (ressonancia)
static const Bipe BIPES_INICIO[]  = { { 2700, 40 } };
static const Bipe BIPES_RADAR[]   = { { 2300, 50 }, { 0, 70 }, { 2900, 50 } };
static const Bipe BIPES_EXCESSO[] = { { 3200, 35 }, { 0, 45 }, { 3200, 35 }, { 0, 45 }, { 3200, 35 } };
static const Bipe BIPES_VOLUME[]  = { { 2700, 25 } };

struct Padrao { const Bipe *bipes; uint8_t quantidade; };
static const Padrao PADROES[NUM_SONS] = {
  { BIPES_INICIO, sizeof(BIPES_INICIO) / sizeof(Bipe) },
  { BIPES_RADAR, sizeof(BIPES_RADAR) / sizeof(Bipe) },
  { BIPES_EXCESSO, sizeof(BIPES_EXCESSO) / sizeof(Bipe) },
  { BIPES_VOLUME, sizeof(BIPES_VOLUME) / sizeof(Bipe) },
};

static const uint8_t BITS_PWM = 10;   // ciclo de trabalho de 0 a 1023
static const uint8_t PEDIDO_PARAR = 0xFF;
static QueueHandle_t filaSons;
static volatile uint8_t volumePorcento = SOM_VOLUME;

static void silenciar() { ledcWrite(PINO_BUZZER, 0); }

static void tarefaBuzzer(void *) {
  uint8_t pedido;
  for (;;) {
    if (xQueueReceive(filaSons, &pedido, portMAX_DELAY) != pdTRUE) continue;
    while (pedido != PEDIDO_PARAR && pedido < NUM_SONS) {
      const Padrao &p = PADROES[pedido];
      bool interrompido = false;
      for (uint8_t i = 0; i < p.quantidade && !interrompido; i++) {
        // 50% de ciclo = volume maximo de um piezo
        uint32_t duty = (512UL * volumePorcento / 100) * SOM_LIMITE_SAIDA / 100;
        if (p.bipes[i].freqHz && duty) {
          ledcChangeFrequency(PINO_BUZZER, p.bipes[i].freqHz, BITS_PWM);
          ledcWrite(PINO_BUZZER, duty);
        } else {
          silenciar();
        }
        // Espera a duracao do bipe, mas atende na hora um pedido novo
        if (xQueueReceive(filaSons, &pedido, pdMS_TO_TICKS(p.bipes[i].duracaoMs)) == pdTRUE)
          interrompido = true;
      }
      silenciar();
      if (!interrompido) break;
    }
    silenciar();
  }
}

void somIniciar() {
  if (!ledcAttach(PINO_BUZZER, 2700, BITS_PWM)) {
    Serial.println("ERRO: falha ao iniciar o PWM do buzzer");
    return;
  }
  silenciar();
  filaSons = xQueueCreate(8, sizeof(uint8_t));
  xTaskCreatePinnedToCore(tarefaBuzzer, "buzzer", 2048, nullptr, 5, nullptr, 0);
}

void somTocar(Som som) {
  uint8_t pedido = som;
  if (filaSons) xQueueSend(filaSons, &pedido, 0);
}

void somParar() {
  uint8_t pedido = PEDIDO_PARAR;
  if (filaSons) xQueueSend(filaSons, &pedido, 0);
}

void somVolume(uint8_t porcento) {
  volumePorcento = porcento > 100 ? 100 : porcento;
}

void somAtualizar(uint32_t) {}

#else
void somIniciar() {}
void somTocar(Som) {}
void somParar() {}
void somVolume(uint8_t) {}
void somAtualizar(uint32_t) {}
#endif
