// Avisos sonoros: chime via I2S (MAX98357A) ou buzzer ativo, conforme SOM_MODO
#pragma once
#include <Arduino.h>
#include "sintetizador.h"

void somIniciar();
void somTocar(Som som);
void somParar();
void somAtualizar(uint32_t agoraMs);  // chamar no loop (usado pelo buzzer)
