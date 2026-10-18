// =============================================================================
//  Sintetizador de "chimes" (sinos suaves) para os avisos sonoros.
//  Nao depende do Arduino: o mesmo codigo gera os .wav de previa no PC
//  (ver ferramentas/gerar_previa_sons.cpp).
// =============================================================================
#pragma once
#include <stdint.h>

#define SINT_TAXA_AMOSTRAGEM 22050

enum Som : uint8_t {
  SOM_INICIO = 0,   // ligou o aparelho: duas notas subindo
  SOM_RADAR,        // radar a frente: "ding-dong-dong" descendente (estilo aeroporto)
  SOM_EXCESSO,      // acima do limite: tres notas agudas e rapidas (repetido)
  NUM_SONS
};

void sintTocar(Som som);   // comeca a tocar (mistura com o que ja estiver tocando)
void sintParar();          // silencia imediatamente
bool sintTocando();
// Gera "n" amostras mono de 16 bits. Escreve zeros quando nao ha som.
void sintGerar(int16_t *saida, int n, float volume);
