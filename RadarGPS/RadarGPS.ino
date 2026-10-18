// =============================================================================
//  RadarGPS - alerta de radares com ESP32-S3 Waveshare 1.47" + GPS Beitian BN-180
//
//  - Le a posicao, velocidade e rumo do GPS (NMEA, via TinyGPSPlus)
//  - Procura na base embarcada (radares_db.h, ~20 mil pontos) o radar mais
//    proximo A FRENTE do veiculo
//  - Mostra velocidade, limite do radar e distancia; toca um chime ao se
//    aproximar e pisca em vermelho com aviso sonoro se estiver acima do limite
//
//  Bibliotecas (Gerenciador de Bibliotecas da IDE):
//    - "TinyGPSPlus" (Mikal Hart)
//    - "GFX Library for Arduino" (moononournation)
//  Placa: "ESP32S3 Dev Module" (pacote esp32 da Espressif) - veja o README.
// =============================================================================
#include <TinyGPSPlus.h>
#include "config.h"
#include "detector.h"
#include "tela.h"
#include "som.h"
#if PLACA_COM_TOUCH
#include "touch_axs5106l.h"
TouchAXS5106L touch;
#endif

TinyGPSPlus gps;
HardwareSerial SerialGPS(1);
DetectorRadar detector;

bool mudo = false;
const uint8_t NIVEIS_BRILHO[] = { 255, 120, 40 };
uint8_t nivelBrilho = 0;

// ---------------------------------------------------------------------------
//  Avisos sonoros (o som em si fica em som.cpp)
// ---------------------------------------------------------------------------
void avisar(Som som) {
  if (!mudo) somTocar(som);
}

// Enquanto estiver acima do limite, repete o aviso de excesso
void atualizarSom(uint32_t agora, EstadoAlerta estado) {
  static uint32_t ultimoExcessoMs = 0;
  static bool estavaEmExcesso = false;
  bool excesso = estado == ALERTA_EXCESSO;
  if (excesso && (!estavaEmExcesso || agora - ultimoExcessoMs >= SOM_REPETIR_EXCESSO_MS)) {
    avisar(SOM_EXCESSO);
    ultimoExcessoMs = agora;
  }
  estavaEmExcesso = excesso;
  somAtualizar(agora);
}

// ---------------------------------------------------------------------------
//  Touch / botao BOOT: toque curto = mudo, toque longo = brilho
// ---------------------------------------------------------------------------
bool pressionadoAntes = false;
bool longoTratado = false;
uint32_t inicioToqueMs = 0;

void tratarEntrada(uint32_t agora) {
  bool pressionado = digitalRead(PINO_BOTAO) == LOW;
#if PLACA_COM_TOUCH
  static uint32_t ultimaLeituraTouch = 0;
  static bool touchAtivo = false;
  if (agora - ultimaLeituraTouch >= 30) {  // ~30 leituras por segundo
    ultimaLeituraTouch = agora;
    touchAtivo = touch.tocando();
  }
  pressionado = pressionado || touchAtivo;
#endif

  if (pressionado && !pressionadoAntes) {
    inicioToqueMs = agora;
    longoTratado = false;
  } else if (pressionado && !longoTratado && agora - inicioToqueMs >= 800) {
    nivelBrilho = (nivelBrilho + 1) % sizeof(NIVEIS_BRILHO);
    telaBrilho(NIVEIS_BRILHO[nivelBrilho]);
    longoTratado = true;
  } else if (!pressionado && pressionadoAntes && !longoTratado && agora - inicioToqueMs >= 40) {
    mudo = !mudo;
    if (mudo) somParar();
  }
  pressionadoAntes = pressionado;
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  SerialGPS.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  pinMode(PINO_BOTAO, INPUT_PULLUP);
  somIniciar();

  if (!telaIniciar()) Serial.println("ERRO: falha ao iniciar a tela");
  telaMensagem("RadarGPS", "iniciando...");
  telaBrilho(NIVEIS_BRILHO[nivelBrilho]);

#if PLACA_COM_TOUCH
  if (!touch.iniciar(TP_SDA, TP_SCL, TP_RST, TP_INT, TP_ENDERECO_I2C))
    Serial.println("Aviso: touch nao encontrado - use o botao BOOT");
#endif
  Serial.println("RadarGPS pronto");
  avisar(SOM_INICIO);
}

void loop() {
  uint32_t agora = millis();

  while (SerialGPS.available()) gps.encode(SerialGPS.read());

  bool gpsFix = gps.location.isValid() && gps.location.age() < 3000;
  float velKmh = gps.speed.isValid() ? gps.speed.kmph() : 0;
  if (velKmh < 3) velKmh = 0;  // parado o GPS oscila alguns km/h

  // A cada nova posicao (1x por segundo no BN-180 de fabrica) roda o detector
  if (gps.location.isUpdated()) {
    const InfoAlvo &a = detector.atualizar(gps.location.lat(), gps.location.lng(), velKmh,
                                           gps.course.deg(), gps.course.isValid(), agora);
    if (a.novo) avisar(SOM_RADAR);

    Serial.printf("lat=%.6f lon=%.6f vel=%.0f rumo=%.0f sat=%u", gps.location.lat(),
                  gps.location.lng(), velKmh, gps.course.deg(), gps.satellites.value());
    if (a.estado != ALERTA_NENHUM)
      Serial.printf(" | %s %ukm/h a %.0fm%s", DetectorRadar::nomeTipo(a.tipo), a.limiteKmh,
                    a.distanciaM, a.estado == ALERTA_EXCESSO ? " EXCESSO!" : "");
    Serial.println();
  }

  // Perdeu o sinal: nao mantem alerta antigo na tela
  InfoAlvo semAlvo = detector.alvo();
  if (!gpsFix) semAlvo.estado = ALERTA_NENHUM;

  tratarEntrada(agora);
  atualizarSom(agora, semAlvo.estado);

  static uint32_t ultimoDesenho = 0;
  if (agora - ultimoDesenho >= 100) {  // 10 quadros por segundo
    ultimoDesenho = agora;
    if (agora > 5000 && gps.charsProcessed() < 10) {
      telaMensagem("SEM DADOS GPS", "confira os fios TX/RX e o baud");
    } else {
      DadosTela d;
      d.gpsFix = gpsFix;
      d.satelites = gps.satellites.isValid() ? gps.satellites.value() : 0;
      d.velKmh = velKmh;
      d.horaValida = gps.time.isValid() && gps.date.isValid() && gps.date.year() > 2000;
      d.hora = (gps.time.hour() + 24 + FUSO_HORARIO) % 24;
      d.minuto = gps.time.minute();
      d.mudo = mudo;
      d.alvo = &semAlvo;
      telaDesenhar(d);
    }
  }
}
