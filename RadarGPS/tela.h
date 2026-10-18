// Interface grafica (tela na horizontal, 320 x 172)
#pragma once
#include <Arduino.h>
#include "detector.h"

struct DadosTela {
  bool gpsFix;
  int satelites;
  float velKmh;
  bool horaValida;
  int hora, minuto;
  bool mudo;
  const InfoAlvo *alvo;
};

bool telaIniciar();
void telaBrilho(uint8_t nivel);   // 0..255
void telaDesenhar(const DadosTela &d);
void telaMensagem(const char *linha1, const char *linha2);
