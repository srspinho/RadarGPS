// =============================================================================
//  TesteAudio - bancada de testes do audio do RadarGPS (sem GPS, sem tela)
//
//  Monitor Serial a 115200. Na IDE: Ferramentas -> "USB CDC On Boot: Enabled".
//  Digite a tecla e aperte Enter. Cada tecla recebida e confirmada na tela.
//
//    a  saida I2S padrao (MAX98357A: BCLK, LRC, DIN)
//    b  saida PDM 1 bit no PINO_DIN (filtro RC -> amplificador analogico, ex. PAM8403)
//    c  saida PWM (onda quadrada simples no PINO_DIN, so para tons)
//    m  passa para o proximo modo (a -> b -> c -> a)
//
//    1  seno 440 Hz            2  seno 1000 Hz          3  seno 250 Hz
//    4  varredura 150 Hz -> 5 kHz (ache a frequencia em que o som "grita")
//    5  onda quadrada 440 Hz (compare com o seno: assim soa um som "estridente")
//    i  chime de inicio        r  chime de radar        e  chime de excesso
//    +  volume +10             -  volume -10
//    s  silencio               ?  menu e configuracao atual
//
//    t  TESTE DE FIACAO: 3 s de onda quadrada de 440 Hz "na unha" (digitalWrite),
//       sem I2S nem PWM. Deve soar ALTO no amplificador. Se nao soar, o problema
//       e a ligacao (pino errado, falta de GND comum, entrada do amplificador).
//    d  liga/desliga o diagnostico: mostra a cada segundo quantas amostras o I2S
//       consumiu (o esperado e ~22050/s; 0 = o audio parou de sair)
//
//  Usa o mesmo sintetizador.cpp do RadarGPS (copiado aqui). Se mudar as notas
//  la, copie o arquivo de novo para ca.
// =============================================================================
#include <ESP_I2S.h>
#include "sintetizador.h"

// --- Pinos ---
#define PINO_BCLK 4    // I2S: BCLK do MAX98357A   | PDM: sai o clock (nao precisa ligar)
#define PINO_LRC  5    // I2S: LRC do MAX98357A    | PDM e PWM: nao usado
#define PINO_DIN  11   // I2S: DIN do MAX98357A    | PDM e PWM: saida de audio

enum Modo : uint8_t { MODO_I2S, MODO_PDM, MODO_PWM, NUM_MODOS };
#define MODO_INICIAL MODO_PDM

static const char *NOME_MODO[] = { "I2S padrao (MAX98357A)", "PDM 1 bit no PINO_DIN",
                                   "PWM onda quadrada no PINO_DIN (so tons)" };

enum Sinal : uint8_t { SINAL_SILENCIO, SINAL_SENO, SINAL_QUADRADA, SINAL_VARREDURA, SINAL_CHIME };

// Tudo roda no loop() (sem tarefas paralelas): mais simples de acompanhar.
// Um bloco de 256 amostras dura ~12 ms, entao o teclado continua responsivo.
static I2SClass *i2s = nullptr;
static Modo modo = NUM_MODOS;
static Sinal sinal = SINAL_SILENCIO;
static float freqHz = 440;
static uint8_t volume = 40;
static bool erroEscritaAvisado = false;
static bool diagnostico = false;
static uint32_t amostrasEnviadas = 0;

static void configurarModo(Modo novo) {
  Serial.printf("Trocando para: %s ...\n", NOME_MODO[novo]);

  // Libera o modo anterior. O objeto I2S e recriado do zero a cada troca.
  if (i2s) {
    i2s->end();
    delete i2s;
    i2s = nullptr;
  }
  if (modo == MODO_PWM) ledcDetach(PINO_DIN);

  bool ok;
  if (novo == MODO_PWM) {
    ok = ledcAttach(PINO_DIN, 1000, 10);
    ledcWriteTone(PINO_DIN, 0);
  } else {
    i2s = new I2SClass();
    if (novo == MODO_I2S) {
      i2s->setPins(PINO_BCLK, PINO_LRC, PINO_DIN);
      ok = i2s->begin(I2S_MODE_STD, SINT_TAXA_AMOSTRAGEM, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    } else {
      i2s->setPinsPdmTx(PINO_BCLK, PINO_DIN);
      ok = i2s->begin(I2S_MODE_PDM_TX, SINT_TAXA_AMOSTRAGEM, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    }
  }
  modo = novo;
  sinal = SINAL_SILENCIO;
  sintParar();
  erroEscritaAvisado = false;
  Serial.printf("Modo ativo: %s -> %s\n", NOME_MODO[modo], ok ? "OK" : "FALHOU ao iniciar!");
  if (ok) Serial.println("(em silencio - escolha um sinal: 1, 2, 3, 4, 5 ou i, r, e)");
}

// Gera um bloco de audio conforme o sinal escolhido
static void gerarBloco(int16_t *mono, int n) {
  static float fase = 0;
  static float freqVarredura = 150;
  const float amp = 0.5f * volume / 100.0f * 32767.0f;

  if (sinal == SINAL_CHIME) {
    sintGerar(mono, n, volume / 100.0f);
    if (!sintTocando()) sinal = SINAL_SILENCIO;
    return;
  }
  if (sinal == SINAL_SILENCIO) {
    for (int i = 0; i < n; i++) mono[i] = 0;
    return;
  }
  for (int i = 0; i < n; i++) {
    float f = freqHz;
    if (sinal == SINAL_VARREDURA) {
      freqVarredura *= 1.0000315f;  // ~5 s de 150 Hz a 5 kHz
      if (freqVarredura > 5000) freqVarredura = 150;
      f = freqVarredura;
    }
    fase += 2.0f * (float)M_PI * f / SINT_TAXA_AMOSTRAGEM;
    if (fase > 2.0f * (float)M_PI) fase -= 2.0f * (float)M_PI;
    float s = (sinal == SINAL_QUADRADA) ? (fase < (float)M_PI ? 1.0f : -1.0f) : sinf(fase);
    mono[i] = (int16_t)(s * amp);
  }
}

static void mostrarMenu() {
  Serial.println();
  Serial.println("=== TesteAudio ===");
  Serial.printf("Modo: %s | volume %u%% | pinos BCLK=%d LRC=%d DIN=%d\n",
                modo < NUM_MODOS ? NOME_MODO[modo] : "-", volume, PINO_BCLK, PINO_LRC, PINO_DIN);
  Serial.println("Saida: a=I2S  b=PDM  c=PWM  m=proximo modo");
  Serial.println("Sinal: 1=440Hz 2=1kHz 3=250Hz 4=varredura 5=quadrada 440Hz");
  Serial.println("Chime: i=inicio r=radar e=excesso | +/- volume | s=silencio | ?=menu");
  Serial.println("Testes: t=fiacao (onda quadrada direta no pino)  d=diagnostico do I2S");
}

// Onda quadrada gerada direto no pino, sem nenhum periferico de audio
static void testeFiacao() {
  Serial.printf("Teste de fiacao: 440 Hz no GPIO%d por 3 s (deve soar ALTO)...\n", PINO_DIN);
  if (i2s) {
    i2s->end();
    delete i2s;
    i2s = nullptr;
  }
  if (modo == MODO_PWM) ledcDetach(PINO_DIN);
  pinMode(PINO_DIN, OUTPUT);
  uint32_t fim = millis() + 3000;
  while ((int32_t)(fim - millis()) > 0) {
    digitalWrite(PINO_DIN, HIGH);
    delayMicroseconds(1136);
    digitalWrite(PINO_DIN, LOW);
    delayMicroseconds(1136);
  }
  Serial.println("Fim do teste de fiacao. Voltando ao modo anterior.");
  Modo anterior = modo;
  modo = NUM_MODOS;
  configurarModo(anterior);
}

static void tocarChime(Som som, const char *nome) {
  sintTocar(som);
  sinal = SINAL_CHIME;
  Serial.printf("Chime %s\n", nome);
}

static void tratarTecla(char c) {
  if (c == '\r' || c == '\n' || c == ' ') return;
  Serial.printf("> tecla '%c'\n", c);
  switch (c) {
    case 'a': case 'A': configurarModo(MODO_I2S); break;
    case 'b': case 'B': configurarModo(MODO_PDM); break;
    case 'c': case 'C': configurarModo(MODO_PWM); break;
    case 'm': case 'M': configurarModo((Modo)((modo + 1) % NUM_MODOS)); break;
    case '1': freqHz = 440;  sinal = SINAL_SENO; Serial.println("Seno 440 Hz"); break;
    case '2': freqHz = 1000; sinal = SINAL_SENO; Serial.println("Seno 1000 Hz"); break;
    case '3': freqHz = 250;  sinal = SINAL_SENO; Serial.println("Seno 250 Hz"); break;
    case '4': sinal = SINAL_VARREDURA; Serial.println("Varredura 150 Hz -> 5 kHz"); break;
    case '5': freqHz = 440;  sinal = SINAL_QUADRADA; Serial.println("Quadrada 440 Hz"); break;
    case 'i': case 'I': tocarChime(SOM_INICIO, "inicio"); break;
    case 'r': case 'R': tocarChime(SOM_RADAR, "radar"); break;
    case 'e': case 'E': tocarChime(SOM_EXCESSO, "excesso"); break;
    case '+': volume = volume >= 90 ? 100 : volume + 10; Serial.printf("Volume %u%%\n", volume); break;
    case '-': volume = volume <= 10 ? 0 : volume - 10;   Serial.printf("Volume %u%%\n", volume); break;
    case 's': case 'S': sinal = SINAL_SILENCIO; sintParar(); Serial.println("Silencio"); break;
    case '?': mostrarMenu(); break;
    case 't': case 'T': testeFiacao(); break;
    case 'd': case 'D':
      diagnostico = !diagnostico;
      Serial.printf("Diagnostico %s\n", diagnostico ? "LIGADO" : "desligado");
      break;
    default: Serial.println("(tecla sem funcao - digite ? para o menu)"); break;
  }
  if (modo == MODO_PWM && (sinal == SINAL_VARREDURA || sinal == SINAL_CHIME))
    Serial.println("Obs.: no modo PWM so funcionam os tons 1, 2, 3 e 5.");
}

void setup() {
  Serial.begin(115200);
  delay(2000);  // tempo para abrir o Monitor Serial
  Serial.println("\nTesteAudio iniciando...");
  configurarModo(MODO_INICIAL);
  mostrarMenu();
}

void loop() {
  while (Serial.available()) tratarTecla((char)Serial.read());

  static uint32_t ultimoRelatorio = 0;
  if (diagnostico && millis() - ultimoRelatorio >= 1000) {
    ultimoRelatorio = millis();
    Serial.printf("[diag] modo=%s sinal=%d volume=%u%% amostras/s=%lu\n",
                  modo < NUM_MODOS ? NOME_MODO[modo] : "-", (int)sinal, volume,
                  (unsigned long)amostrasEnviadas);
    amostrasEnviadas = 0;
  }

  if (modo == MODO_PWM) {
    // PWM: so liga/desliga um tom (sem volume)
    static uint32_t ultimo = UINT32_MAX;
    uint32_t f = (sinal == SINAL_SENO || sinal == SINAL_QUADRADA) ? (uint32_t)freqHz : 0;
    if (f != ultimo) {
      ledcWriteTone(PINO_DIN, f);
      ultimo = f;
    }
    delay(10);
    return;
  }

  static int16_t mono[256];
  static int16_t estereo[512];
  gerarBloco(mono, 256);
  for (int i = 0; i < 256; i++) estereo[2 * i] = estereo[2 * i + 1] = mono[i];
  size_t escritos = i2s ? i2s->write((uint8_t *)estereo, sizeof(estereo)) : 0;
  amostrasEnviadas += escritos / 4;  // 4 bytes por amostra estereo de 16 bits
  if (escritos == 0) {
    if (!erroEscritaAvisado) {
      Serial.println("ERRO: o I2S nao esta aceitando dados (veja a mensagem de inicio do modo)");
      erroEscritaAvisado = true;
    }
    delay(10);  // evita travar o loop se o I2S falhou
  }
}
