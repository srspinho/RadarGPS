// Gera arquivos .wav com os avisos sonoros, para ouvir no PC antes de montar.
//   g++ -O2 -I../RadarGPS gerar_previa_sons.cpp ../RadarGPS/sintetizador.cpp -o previa && ./previa
#include <stdio.h>
#include <string.h>
#include "sintetizador.h"

static void gravarWav(const char *nome, Som som, int repeticoes, int intervaloMs) {
  const int taxa = SINT_TAXA_AMOSTRAGEM;
  const int total = taxa * (repeticoes * intervaloMs / 1000 + 2);
  static int16_t amostras[SINT_TAXA_AMOSTRAGEM * 12];
  sintParar();
  for (int i = 0; i < total; i += 256) {
    if (i % (taxa * intervaloMs / 1000) < 256 && i / (taxa * intervaloMs / 1000) < repeticoes) sintTocar(som);
    sintGerar(amostras + i, 256, 1.0f);
  }
  FILE *f = fopen(nome, "wb");
  uint32_t dados = total * 2, tam = 36 + dados, fmtTam = 16, taxaBytes = taxa * 2;
  uint16_t pcm = 1, canais = 1, bloco = 2, bits = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&tam, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  fwrite(&fmtTam, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&canais, 2, 1, f);
  fwrite(&taxa, 4, 1, f); fwrite(&taxaBytes, 4, 1, f); fwrite(&bloco, 2, 1, f); fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&dados, 4, 1, f); fwrite(amostras, 2, total, f);
  fclose(f);
  printf("%s gravado\n", nome);
}

int main() {
  gravarWav("som_inicio.wav", SOM_INICIO, 1, 1000);
  gravarWav("som_radar.wav", SOM_RADAR, 1, 1000);
  gravarWav("som_excesso.wav", SOM_EXCESSO, 4, 1500);  // repete enquanto estiver acima do limite
  return 0;
}
