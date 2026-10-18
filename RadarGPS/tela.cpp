#include "tela.h"
#include "config.h"
#include <Arduino_GFX_Library.h>

// O desenho e feito num "canvas" na RAM e enviado inteiro para o display,
// assim a tela nao pisca. (172 x 320 x 2 bytes = 110 KB)
// O buffer fica na orientacao nativa (em pe); a rotacao do canvas faz o desenho
// sair na horizontal (320 x 172).
static Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, GFX_NOT_DEFINED);
// O JD9853 da versao touch aceita os comandos padrao (MIPI DCS) do ST7789.
// Se a imagem ficar errada, veja a secao "Tela" do README.
static Arduino_GFX *display = new Arduino_ST7789(bus, LCD_RST, 0, LCD_IPS, LCD_LARGURA, LCD_ALTURA,
                                                  LCD_OFFSET_X, LCD_OFFSET_Y, LCD_OFFSET_X, LCD_OFFSET_Y);
static Arduino_Canvas *gfx = new Arduino_Canvas(LCD_LARGURA, LCD_ALTURA, display);

static const uint16_t COR_FUNDO    = RGB565_BLACK;
static const uint16_t COR_TEXTO    = RGB565_WHITE;
static const uint16_t COR_CINZA    = 0x8410;
static const uint16_t COR_VERDE    = 0x07E0;
static const uint16_t COR_AMARELO  = 0xFFE0;
static const uint16_t COR_LARANJA  = 0xFD20;
static const uint16_t COR_VERMELHO = 0xF800;

#if ESP_ARDUINO_VERSION_MAJOR < 3
static const int CANAL_BL = 0;
#endif

static const char *nomeCurto(uint8_t tipo) {
  switch (tipo) {
    case TIPO_RADAR_FIXO:         return "RADAR FIXO";
    case TIPO_SEMAFORO_RADAR:     return "SEMAF+RADAR";
    case TIPO_RADAR_MOVEL:        return "RADAR MOVEL";
    case TIPO_SEMAFORO_CAMERA:    return "SEMAF+CAM.";
    case TIPO_POLICIA_RODOVIARIA: return "POLICIA ROD";
    case TIPO_PEDAGIO:            return "PEDAGIO";
    default:                      return "?";
  }
}

// Layout horizontal (320 x 172):
//   barra de status no topo; velocidade a esquerda; alerta a direita;
//   barra de aproximacao ao longo da borda de baixo.
static const int LARG = 320;
static const int ALT = 172;
static const int CX_VEL = 90;      // centro da coluna da velocidade
static const int CX_ALERTA = 250;  // centro da coluna do alerta

// A fonte padrao tem 6 x 8 pixels por caractere (multiplicado por "tamanho")
static void textoCentro(const char *txt, int cx, int y, uint8_t tamanho, uint16_t cor) {
  int largura = strlen(txt) * 6 * tamanho;
  gfx->setTextSize(tamanho);
  gfx->setTextColor(cor);
  gfx->setCursor(cx - largura / 2, y);
  gfx->print(txt);
}

// Envia o canvas para o display, espelhando a imagem se configurado no config.h
static void enviarTela() {
#if LCD_ESPELHAR_X || LCD_ESPELHAR_Y
  uint16_t *fb = gfx->getFramebuffer();
  if (fb) {
#if LCD_ESPELHAR_X
    for (int y = 0; y < LCD_ALTURA; y++) {
      uint16_t *ini = fb + y * LCD_LARGURA, *fim = ini + LCD_LARGURA - 1;
      while (ini < fim) { uint16_t t = *ini; *ini++ = *fim; *fim-- = t; }
    }
#endif
#if LCD_ESPELHAR_Y
    for (int y = 0; y < LCD_ALTURA / 2; y++) {
      uint16_t *a = fb + y * LCD_LARGURA, *b = fb + (LCD_ALTURA - 1 - y) * LCD_LARGURA;
      for (int x = 0; x < LCD_LARGURA; x++) { uint16_t t = a[x]; a[x] = b[x]; b[x] = t; }
    }
#endif
  }
#endif
  gfx->flush();
}

bool telaIniciar() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(LCD_BL, 5000, 8);
#else
  ledcSetup(CANAL_BL, 5000, 8);
  ledcAttachPin(LCD_BL, CANAL_BL);
#endif
  telaBrilho(0);
  if (!gfx->begin()) return false;
  gfx->setRotation(LCD_ROTACAO);
  gfx->fillScreen(COR_FUNDO);
  enviarTela();
  return true;
}

void telaBrilho(uint8_t nivel) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(LCD_BL, nivel);
#else
  ledcWrite(CANAL_BL, nivel);
#endif
}

void telaMensagem(const char *linha1, const char *linha2) {
  gfx->fillScreen(COR_FUNDO);
  textoCentro(linha1, LARG / 2, 66, 2, COR_TEXTO);
  if (linha2) textoCentro(linha2, LARG / 2, 96, 1, COR_CINZA);
  enviarTela();
}

static void desenharPlaca(int cx, int cy, uint8_t limite) {
  gfx->fillCircle(cx, cy, 40, COR_VERMELHO);
  gfx->fillCircle(cx, cy, 31, RGB565_WHITE);
  char txt[8];
  if (limite > 0) snprintf(txt, sizeof(txt), "%u", limite);
  else strcpy(txt, "!");
  uint8_t tam = strlen(txt) >= 3 ? 3 : 4;
  int larg = strlen(txt) * 6 * tam;
  gfx->setTextSize(tam);
  gfx->setTextColor(RGB565_BLACK);
  gfx->setCursor(cx - larg / 2 + tam / 2, cy - 4 * tam + tam / 2);
  gfx->print(txt);
}

void telaDesenhar(const DadosTela &d) {
  const InfoAlvo *a = d.alvo;
  bool temAlvo = a && a->estado != ALERTA_NENHUM;
  bool excesso = temAlvo && a->estado == ALERTA_EXCESSO;
  // Em excesso o alerta pisca em vermelho (4 vezes por segundo)
  bool pisca = excesso && ((millis() / 250) % 2 == 0);
#if LCD_ALERTA_TELA_CHEIA
  bool fundoVermelho = pisca;
#else
  bool fundoVermelho = false;
#endif

  gfx->fillScreen(fundoVermelho ? COR_VERMELHO : COR_FUNDO);
  if (pisca && !fundoVermelho) {
    // Moldura vermelha piscando: chama a atencao sem clarear a tela a noite
    for (int i = 0; i < 6; i++) gfx->drawRect(i, i, LARG - 2 * i, ALT - 2 * i, COR_VERMELHO);
  }
  char txt[24];

  // --- Barra de status ---
  gfx->setTextSize(2);
  gfx->setTextColor(d.gpsFix ? COR_VERDE : COR_LARANJA);
  gfx->setCursor(8, 8);
  snprintf(txt, sizeof(txt), "%dsat", d.satelites);
  gfx->print(txt);
  if (d.mudo) textoCentro("MUDO", LARG / 2, 8, 2, fundoVermelho ? COR_TEXTO : COR_VERMELHO);
  if (d.horaValida) {
    snprintf(txt, sizeof(txt), "%02d:%02d", d.hora, d.minuto);
    gfx->setTextColor(COR_TEXTO);
    gfx->setCursor(LARG - 5 * 12 - 8, 8);
    gfx->print(txt);
  }
  gfx->drawFastHLine(8, 28, LARG - 16, COR_CINZA);
  gfx->drawFastVLine(180, 36, 112, COR_CINZA);

  // --- Velocidade (coluna da esquerda) ---
  uint16_t corVel = COR_TEXTO;
  if (!d.gpsFix) {
    corVel = COR_CINZA;
  } else if (temAlvo && a->limiteKmh > 0) {
    if (excesso) corVel = fundoVermelho ? COR_TEXTO : COR_VERMELHO;
    else if (d.velKmh >= a->limiteKmh - MARGEM_AMARELO_KMH) corVel = COR_AMARELO;
  }
  if (d.gpsFix) snprintf(txt, sizeof(txt), "%d", (int)(d.velKmh + 0.5f));
  else strcpy(txt, "--");
  textoCentro(txt, CX_VEL, 44, 9, corVel);
  textoCentro("km/h", CX_VEL, 126, 2, fundoVermelho ? COR_TEXTO : COR_CINZA);

  // --- Alerta (coluna da direita) ---
  if (!d.gpsFix) {
    textoCentro("BUSCANDO", CX_ALERTA, 68, 2, COR_LARANJA);
    textoCentro("SATELITES", CX_ALERTA, 90, 2, COR_LARANJA);
    textoCentro("antena p/ o ceu", CX_ALERTA, 122, 1, COR_CINZA);
  } else if (!temAlvo) {
    textoCentro("VIA LIVRE", CX_ALERTA, 72, 2, COR_VERDE);
    textoCentro("nenhum radar", CX_ALERTA, 100, 1, COR_CINZA);
    textoCentro("a frente", CX_ALERTA, 112, 1, COR_CINZA);
  } else {
    desenharPlaca(CX_ALERTA, 76, a->limiteKmh);

    uint16_t corInfo = fundoVermelho ? COR_TEXTO : (excesso ? COR_VERMELHO : COR_AMARELO);
    textoCentro(excesso ? "REDUZA!" : nomeCurto(a->tipo), CX_ALERTA, 122, 2, corInfo);

    if (a->distanciaM >= 1000) snprintf(txt, sizeof(txt), "%.1f km", a->distanciaM / 1000.0f);
    else snprintf(txt, sizeof(txt), "%d m", ((int)a->distanciaM / 10) * 10);
    textoCentro(txt, CX_ALERTA, 142, 2, COR_TEXTO);

    // Barra na borda de baixo que enche conforme o radar se aproxima
    float frac = 1.0f - a->distanciaM / a->distanciaAlertaM;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    gfx->drawRect(10, 162, LARG - 20, 8, COR_CINZA);
    gfx->fillRect(12, 164, (int)((LARG - 24) * frac), 4, fundoVermelho ? COR_TEXTO : corInfo);
  }

  enviarTela();
}
