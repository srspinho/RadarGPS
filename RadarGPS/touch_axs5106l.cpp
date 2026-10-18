#include "touch_axs5106l.h"
#include <Wire.h>

bool TouchAXS5106L::iniciar(int sda, int scl, int rst, int intr, uint8_t endereco) {
  _endereco = endereco;
  if (intr >= 0) pinMode(intr, INPUT_PULLUP);
  if (rst >= 0) {
    pinMode(rst, OUTPUT);
    digitalWrite(rst, LOW);
    delay(20);
    digitalWrite(rst, HIGH);
    delay(100);
  }
  Wire.begin(sda, scl, 400000);
  Wire.beginTransmission(_endereco);
  _ok = (Wire.endTransmission() == 0);
  return _ok;
}

bool TouchAXS5106L::tocando(uint16_t *x, uint16_t *y) {
  if (!_ok) return false;
  // Registrador 0x01: [gesto, n_pontos, xH, xL, yH, yL, ...]
  Wire.beginTransmission(_endereco);
  Wire.write(0x01);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)_endereco, 6) != 6) return false;
  uint8_t d[6];
  for (int i = 0; i < 6; i++) d[i] = Wire.read();

  uint8_t pontos = d[1] & 0x0F;
  if (pontos == 0) return false;
  if (x) *x = ((d[2] & 0x0F) << 8) | d[3];
  if (y) *y = ((d[4] & 0x0F) << 8) | d[5];
  return true;
}
