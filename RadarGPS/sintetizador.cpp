#include "sintetizador.h"
#include <math.h>
#include <string.h>

// Cada nota e um "sino": a frequencia fundamental mais 3 harmonicos, cada um
// com um decaimento exponencial proprio (os agudos somem antes, como num
// vibrafone). Um ataque de alguns milissegundos evita estalos.
static const int NUM_PARCIAIS = 4;
static const float RAZAO_PARCIAL[NUM_PARCIAIS] = { 1.0f, 2.0f, 3.0f, 4.2f };
static const float AMP_PARCIAL[NUM_PARCIAIS]   = { 1.0f, 0.35f, 0.12f, 0.06f };
static const float TEMPO_PARCIAL[NUM_PARCIAIS] = { 1.0f, 0.5f, 0.3f, 0.2f };  // x decaimento da nota
static const int AMOSTRAS_ATAQUE = SINT_TAXA_AMOSTRAGEM * 4 / 1000;           // 4 ms

struct Nota {
  float freqHz;
  uint16_t inicioMs;
  float amplitude;
  float decaimentoS;   // tempo para o volume cair a ~37%
};

// Notas (Hz): C5 523,25  E5 659,25  G5 783,99  A5 880,00  C6 1046,50  E6 1318,51
static const Nota NOTAS_INICIO[] = {
  { 523.25f, 0, 0.45f, 0.35f }, { 783.99f, 130, 0.45f, 0.50f },
};
static const Nota NOTAS_RADAR[] = {
  { 783.99f, 0, 0.80f, 0.90f }, { 659.25f, 300, 0.80f, 0.90f }, { 523.25f, 600, 0.85f, 1.20f },
};
static const Nota NOTAS_EXCESSO[] = {
  { 1046.50f, 0, 0.70f, 0.22f }, { 1318.51f, 130, 0.70f, 0.22f }, { 1046.50f, 260, 0.70f, 0.30f },
};

struct Sequencia { const Nota *notas; int quantidade; };
static const Sequencia SEQUENCIAS[NUM_SONS] = {
  { NOTAS_INICIO, sizeof(NOTAS_INICIO) / sizeof(Nota) },
  { NOTAS_RADAR, sizeof(NOTAS_RADAR) / sizeof(Nota) },
  { NOTAS_EXCESSO, sizeof(NOTAS_EXCESSO) / sizeof(Nota) },
};

// --- Tabela de seno (evita chamar sinf() milhares de vezes por segundo) ---
static const int BITS_TABELA = 10;
static const int TAM_TABELA = 1 << BITS_TABELA;
static float tabelaSeno[TAM_TABELA];
static bool tabelaPronta = false;

static void prepararTabela() {
  for (int i = 0; i < TAM_TABELA; i++) tabelaSeno[i] = sinf(2.0f * (float)M_PI * i / TAM_TABELA);
  tabelaPronta = true;
}

// --- Vozes: cada nota em execucao ocupa uma voz ---
struct Voz {
  bool ativa;
  uint32_t espera;                  // amostras ate a nota comecar
  uint32_t idade;                   // amostras desde o inicio da nota
  uint32_t fase[NUM_PARCIAIS];      // acumulador de fase (32 bits = uma volta)
  uint32_t passo[NUM_PARCIAIS];
  float envelope[NUM_PARCIAIS];
  float fatorDecaimento[NUM_PARCIAIS];
};
static const int MAX_VOZES = 8;
static Voz vozes[MAX_VOZES];

static void iniciarNota(const Nota &n) {
  // Usa uma voz livre; se nao houver, reaproveita a mais antiga
  Voz *v = &vozes[0];
  for (int i = 0; i < MAX_VOZES; i++) {
    if (!vozes[i].ativa) { v = &vozes[i]; break; }
    if (vozes[i].idade > v->idade) v = &vozes[i];
  }
  memset(v, 0, sizeof(Voz));
  v->ativa = true;
  v->espera = (uint32_t)n.inicioMs * SINT_TAXA_AMOSTRAGEM / 1000;
  for (int p = 0; p < NUM_PARCIAIS; p++) {
    float f = n.freqHz * RAZAO_PARCIAL[p];
    v->passo[p] = (uint32_t)(f / SINT_TAXA_AMOSTRAGEM * 4294967296.0);
    v->envelope[p] = n.amplitude * AMP_PARCIAL[p];
    float tau = n.decaimentoS * TEMPO_PARCIAL[p] * SINT_TAXA_AMOSTRAGEM;
    v->fatorDecaimento[p] = expf(-1.0f / tau);
  }
}

void sintTocar(Som som) {
  if (!tabelaPronta) prepararTabela();
  if (som >= NUM_SONS) return;
  const Sequencia &s = SEQUENCIAS[som];
  for (int i = 0; i < s.quantidade; i++) iniciarNota(s.notas[i]);
}

void sintParar() {
  for (int i = 0; i < MAX_VOZES; i++) vozes[i].ativa = false;
}

bool sintTocando() {
  for (int i = 0; i < MAX_VOZES; i++)
    if (vozes[i].ativa) return true;
  return false;
}

void sintGerar(int16_t *saida, int n, float volume) {
  if (!tabelaPronta) prepararTabela();
  const float ganho = 32767.0f * 0.30f * volume;  // folga para varias notas somadas
  for (int i = 0; i < n; i++) {
    float soma = 0;
    for (int k = 0; k < MAX_VOZES; k++) {
      Voz &v = vozes[k];
      if (!v.ativa) continue;
      if (v.espera) { v.espera--; continue; }

      float ataque = v.idade < (uint32_t)AMOSTRAS_ATAQUE ? (float)v.idade / AMOSTRAS_ATAQUE : 1.0f;
      float amostra = 0;
      for (int p = 0; p < NUM_PARCIAIS; p++) {
        amostra += tabelaSeno[v.fase[p] >> (32 - BITS_TABELA)] * v.envelope[p];
        v.fase[p] += v.passo[p];
        v.envelope[p] *= v.fatorDecaimento[p];
      }
      soma += amostra * ataque;
      v.idade++;
      if (v.envelope[0] < 0.0005f) v.ativa = false;  // ja inaudivel
    }
    float s = soma * ganho;
    if (s > 32767.0f) s = 32767.0f;
    if (s < -32768.0f) s = -32768.0f;
    saida[i] = (int16_t)s;
  }
}
