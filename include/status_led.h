// status_led.h
// LED intégrée de la carte, utilisée comme témoin de vie.
// Sur la Super Mini, elle est câblée vers le 3,3 V : LOW l'allume, HIGH l'éteint.

#pragma once
#include <Arduino.h>

namespace StatusLed {
  void begin();
  void on();
  void off();
  // Allume la LED pour la durée donnée, sans bloquer : appeler update() dans loop().
  void flash(uint16_t durationMs);
  void update();
}
