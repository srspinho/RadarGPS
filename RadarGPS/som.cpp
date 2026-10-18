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

static void tarefaAudio(void *) {
  static int16_t mono[256];
  static int16_t estereo[512];
  for (;;) {
    uint8_t pedido;
    while (xQueueReceive(filaSons, &pedido, 0) == pdTRUE) {
      if (pedido == PEDIDO_PARAR) sintParar();
      else sintTocar((Som)pedido);
    }
    sintGerar(mono, 256, SOM_VOLUME / 100.0f);
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

#else
void somIniciar() {}
void somTocar(Som) {}
void somParar() {}
void somAtualizar(uint32_t) {}
#endif
