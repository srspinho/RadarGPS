// =============================================================================
//  Detector de aproximacao de radares.
//  Nao depende do Arduino: pode ser testado no PC (ver ferramentas/teste_detector.cpp).
// =============================================================================
#pragma once
#include <stdint.h>

// Mesma ordem da lista TIPOS em ferramentas/gerar_radares.py
enum TipoRadar : uint8_t {
  TIPO_RADAR_FIXO = 0,
  TIPO_SEMAFORO_RADAR,
  TIPO_RADAR_MOVEL,
  TIPO_SEMAFORO_CAMERA,
  TIPO_POLICIA_RODOVIARIA,
  TIPO_PEDAGIO,
  NUM_TIPOS
};

enum EstadoAlerta : uint8_t {
  ALERTA_NENHUM = 0,
  ALERTA_APROXIMANDO,   // ha um radar a frente e a velocidade esta dentro do limite
  ALERTA_EXCESSO        // ha um radar a frente e a velocidade esta ACIMA do limite
};

struct InfoAlvo {
  int32_t indice;        // -1 = nenhum radar a frente
  uint8_t tipo;
  uint8_t limiteKmh;     // 0 = sem limite cadastrado (pedagio, semaforo com camera...)
  float distanciaM;
  float distanciaAlertaM;
  EstadoAlerta estado;
  bool novo;             // true somente na primeira atualizacao em que o alvo aparece
};

class DetectorRadar {
public:
  DetectorRadar();

  // Chamar a cada nova posicao do GPS.
  //  latGraus/lonGraus: posicao   velKmh: velocidade   rumoGraus: curso (0 = norte)
  //  rumoValido: false se o GPS ainda nao forneceu curso   agoraMs: millis()
  const InfoAlvo &atualizar(double latGraus, double lonGraus, float velKmh,
                            float rumoGraus, bool rumoValido, uint32_t agoraMs);

  const InfoAlvo &alvo() const { return _alvo; }
  static const char *nomeTipo(uint8_t tipo);

private:
  struct Ignorado { int32_t indice; uint32_t ateMs; };
  static const int MAX_IGNORADOS = 16;

  bool tipoHabilitado(uint8_t tipo) const;
  bool estaIgnorado(int32_t indice, uint32_t agoraMs) const;
  void ignorar(int32_t indice, uint32_t agoraMs);
  void medir(int32_t indice, double lat, double lon, float &distM, float &rumoM) const;

  InfoAlvo _alvo;
  float _menorDistancia;    // menor distancia ja vista ate o alvo atual
  float _ultimoRumo;
  bool _temRumo;
  Ignorado _ignorados[MAX_IGNORADOS];
  int _proxIgnorado;
};
