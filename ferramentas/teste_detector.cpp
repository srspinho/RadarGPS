// Teste do detector no PC (sem Arduino): simula um carro passando por um radar.
//   g++ -O2 -I../RadarGPS teste_detector.cpp ../RadarGPS/detector.cpp -o teste && ./teste
#include <stdio.h>
#include <math.h>
#include "detector.h"

static void simular(const char *titulo, double latRadar, double lonRadar, float velKmh,
                    float rumo, float inicioM, float fimM) {
  printf("\n== %s (%.0f km/h, rumo %.0f) ==\n", titulo, velKmh, rumo);
  DetectorRadar det;
  double passo = velKmh / 3.6;  // metros por segundo (GPS a 1 Hz)
  int ultimoEstado = -1;
  for (double s = inicioM; s <= fimM; s += passo) {
    // posicao a "s" metros do radar ao longo do rumo (negativo = antes do radar)
    double dN = s * cos(rumo * M_PI / 180), dE = s * sin(rumo * M_PI / 180);
    double lat = latRadar + dN / 111195.0;
    double lon = lonRadar + dE / (111195.0 * cos(latRadar * M_PI / 180));
    uint32_t ms = (uint32_t)((s - inicioM) / passo * 1000);
    const InfoAlvo &a = det.atualizar(lat, lon, velKmh, rumo, true, ms);
    if (a.estado != ultimoEstado || a.novo) {
      printf("  pos %6.0f m: estado=%d %s lim=%d dist=%.0f m (alerta a %.0f m)\n", s, a.estado,
             a.indice >= 0 ? DetectorRadar::nomeTipo(a.tipo) : "-", a.limiteKmh, a.distanciaM,
             a.distanciaAlertaM);
      ultimoEstado = a.estado;
    }
  }
}

int main() {
  // Radar Fixo 30 km/h do CSV: -43.030287,-22.665031
  simular("indo em direcao ao radar, acima do limite", -22.665031, -43.030287, 50, 0, -900, 400);
  simular("indo em direcao ao radar, dentro do limite", -22.665031, -43.030287, 28, 0, -900, 400);
  simular("afastando-se do radar (radar atras)", -22.665031, -43.030287, 50, 0, 20, 900);
  return 0;
}
