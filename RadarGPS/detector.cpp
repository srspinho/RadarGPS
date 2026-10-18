#include "detector.h"
#include "config.h"
#include "radares_db.h"
#include <math.h>

static const double RAIO_TERRA_M = 6371000.0;
static const double GRAU_RAD = 0.017453292519943295;

// Janela de busca em latitude: 0,02 grau ~ 2,2 km (maior que DIST_ALERTA_MAX_M)
static const int32_t JANELA_LAT_E6 = 20000;

// Raio em que radares vizinhos sao considerados "o mesmo ponto" (ex.: um radar
// para cada sentido da via). Ao passar por um, os vizinhos tambem sao ignorados.
static const float RAIO_VIZINHOS_M = 80.0f;

// Por quanto tempo um radar ja ultrapassado deixa de gerar alerta
static const uint32_t TEMPO_IGNORAR_MS = 120000;

static float difAngulo(float a, float b) {
  float d = fmodf(fabsf(a - b), 360.0f);
  return d > 180.0f ? 360.0f - d : d;
}

// Primeiro indice com RADAR_LAT[i] >= valor
static int32_t buscaInferior(int32_t valor) {
  int32_t ini = 0, fim = NUM_RADARES;
  while (ini < fim) {
    int32_t meio = (ini + fim) / 2;
    if (RADAR_LAT[meio] < valor) ini = meio + 1;
    else fim = meio;
  }
  return ini;
}

DetectorRadar::DetectorRadar()
  : _menorDistancia(1e9f), _ultimoRumo(0), _temRumo(false), _proxIgnorado(0) {
  _alvo.indice = -1;
  _alvo.tipo = 0;
  _alvo.limiteKmh = 0;
  _alvo.distanciaM = 0;
  _alvo.distanciaAlertaM = 0;
  _alvo.estado = ALERTA_NENHUM;
  _alvo.novo = false;
  for (int i = 0; i < MAX_IGNORADOS; i++) _ignorados[i].indice = -1;
}

const char *DetectorRadar::nomeTipo(uint8_t tipo) {
  switch (tipo) {
    case TIPO_RADAR_FIXO:         return "Radar Fixo";
    case TIPO_SEMAFORO_RADAR:     return "Semaforo c/ Radar";
    case TIPO_RADAR_MOVEL:        return "Radar Movel";
    case TIPO_SEMAFORO_CAMERA:    return "Semaforo c/ Camera";
    case TIPO_POLICIA_RODOVIARIA: return "Policia Rodoviaria";
    case TIPO_PEDAGIO:            return "Pedagio";
    default:                      return "?";
  }
}

bool DetectorRadar::tipoHabilitado(uint8_t tipo) const {
  switch (tipo) {
    case TIPO_RADAR_FIXO:         return ALERTA_RADAR_FIXO;
    case TIPO_SEMAFORO_RADAR:     return ALERTA_SEMAFORO_RADAR;
    case TIPO_RADAR_MOVEL:        return ALERTA_RADAR_MOVEL;
    case TIPO_SEMAFORO_CAMERA:    return ALERTA_SEMAFORO_CAMERA;
    case TIPO_POLICIA_RODOVIARIA: return ALERTA_POLICIA_RODOVIARIA;
    case TIPO_PEDAGIO:            return ALERTA_PEDAGIO;
    default:                      return false;
  }
}

bool DetectorRadar::estaIgnorado(int32_t indice, uint32_t agoraMs) const {
  for (int i = 0; i < MAX_IGNORADOS; i++) {
    if (_ignorados[i].indice == indice && (int32_t)(_ignorados[i].ateMs - agoraMs) > 0)
      return true;
  }
  return false;
}

void DetectorRadar::ignorar(int32_t indice, uint32_t agoraMs) {
  _ignorados[_proxIgnorado].indice = indice;
  _ignorados[_proxIgnorado].ateMs = agoraMs + TEMPO_IGNORAR_MS;
  _proxIgnorado = (_proxIgnorado + 1) % MAX_IGNORADOS;
}

// Distancia (aproximacao equiretangular, otima para poucos km) e rumo ate o radar
void DetectorRadar::medir(int32_t i, double lat, double lon, float &distM, float &rumo) const {
  double rLat = RADAR_LAT[i] * 1e-6, rLon = RADAR_LON[i] * 1e-6;
  double dy = (rLat - lat) * GRAU_RAD * RAIO_TERRA_M;
  double dx = (rLon - lon) * GRAU_RAD * RAIO_TERRA_M * cos((lat + rLat) * 0.5 * GRAU_RAD);
  distM = (float)sqrt(dx * dx + dy * dy);
  rumo = (float)(atan2(dx, dy) / GRAU_RAD);
  if (rumo < 0) rumo += 360.0f;
}

const InfoAlvo &DetectorRadar::atualizar(double lat, double lon, float velKmh,
                                         float rumoGraus, bool rumoValido, uint32_t agoraMs) {
  bool emMovimento = rumoValido && velKmh >= VEL_MIN_RUMO_KMH;
  if (emMovimento) {
    _ultimoRumo = rumoGraus;
    _temRumo = true;
  }

  float distAlerta = velKmh / 3.6f * TEMPO_ANTECEDENCIA_S;
  if (distAlerta < DIST_ALERTA_MIN_M) distAlerta = DIST_ALERTA_MIN_M;
  if (distAlerta > DIST_ALERTA_MAX_M) distAlerta = DIST_ALERTA_MAX_M;

  _alvo.novo = false;

  // 1) Acompanha o alvo atual e verifica se ja passamos por ele
  if (_alvo.indice >= 0) {
    float dist, rumo;
    medir(_alvo.indice, lat, lon, dist, rumo);
    if (dist < _menorDistancia) _menorDistancia = dist;

    bool passou =
      (_menorDistancia < 80.0f && dist > _menorDistancia + 25.0f) ||          // chegou perto e esta se afastando
      (emMovimento && dist > 30.0f && difAngulo(_ultimoRumo, rumo) > 100.0f) || // ficou para tras
      (dist > distAlerta + 300.0f);                                          // saiu da rota

    if (passou) {
      // Ignora o radar ultrapassado e seus vizinhos (ex.: radar do sentido oposto)
      int32_t latRef = RADAR_LAT[_alvo.indice];
      for (int32_t i = buscaInferior(latRef - 1000); i < NUM_RADARES && RADAR_LAT[i] <= latRef + 1000; i++) {
        float d, r;
        medir(i, RADAR_LAT[_alvo.indice] * 1e-6, RADAR_LON[_alvo.indice] * 1e-6, d, r);
        if (d <= RAIO_VIZINHOS_M) ignorar(i, agoraMs);
      }
      _alvo.indice = -1;
    } else {
      _alvo.distanciaM = dist;
    }
  }

  // 2) Sem alvo: procura o radar mais proximo a frente
  if (_alvo.indice < 0) {
    int32_t latE6 = (int32_t)lround(lat * 1e6);
    int32_t lonE6 = (int32_t)lround(lon * 1e6);
    int32_t janelaLon = (int32_t)(JANELA_LAT_E6 / cos(lat * GRAU_RAD));

    int32_t melhor = -1;
    float melhorDist = 1e9f;
    for (int32_t i = buscaInferior(latE6 - JANELA_LAT_E6);
         i < NUM_RADARES && RADAR_LAT[i] <= latE6 + JANELA_LAT_E6; i++) {
      if (RADAR_LON[i] < lonE6 - janelaLon || RADAR_LON[i] > lonE6 + janelaLon) continue;
      if (!tipoHabilitado(RADAR_TIPO[i])) continue;

      float dist, rumo;
      medir(i, lat, lon, dist, rumo);
      if (dist > distAlerta || dist >= melhorDist) continue;
      if (estaIgnorado(i, agoraMs)) continue;

      if (_temRumo) {
        if (difAngulo(_ultimoRumo, rumo) > ANGULO_CONE_GRAUS) continue;
      } else if (dist > DIST_ALERTA_MIN_M / 2) {
        continue;  // ainda sem rumo (acabou de ligar): so avisa radares bem proximos
      }
      melhor = i;
      melhorDist = dist;
    }

    if (melhor >= 0) {
      _alvo.indice = melhor;
      _alvo.tipo = RADAR_TIPO[melhor];
      _alvo.limiteKmh = RADAR_LIMITE[melhor];
      _alvo.distanciaM = melhorDist;
      _alvo.novo = true;
      _menorDistancia = melhorDist;
    }
  }

  // 3) Estado do alerta
  _alvo.distanciaAlertaM = distAlerta;
  if (_alvo.indice < 0) {
    _alvo.estado = ALERTA_NENHUM;
  } else if (_alvo.limiteKmh > 0 && velKmh > _alvo.limiteKmh + TOLERANCIA_KMH) {
    _alvo.estado = ALERTA_EXCESSO;
  } else {
    _alvo.estado = ALERTA_APROXIMANDO;
  }
  return _alvo;
}
