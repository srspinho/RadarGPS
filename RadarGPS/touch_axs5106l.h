// Leitura minima do controlador de touch AXS5106L (I2C).
// So precisamos saber se ha um dedo na tela, entao a posicao e opcional.
#pragma once
#include <Arduino.h>

class TouchAXS5106L {
public:
  bool iniciar(int sda, int scl, int rst, int intr, uint8_t endereco);
  // true enquanto houver um dedo encostado. x/y podem ser nullptr.
  bool tocando(uint16_t *x = nullptr, uint16_t *y = nullptr);
  bool disponivel() const { return _ok; }

private:
  uint8_t _endereco = 0;
  bool _ok = false;
};
