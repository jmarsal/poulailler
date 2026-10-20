// light_sensor.h
// Capteur de lumière KY-018 : photorésistance + résistance fixe de 10 kΩ en pont diviseur.
// Le module n'est alimenté que pendant la mesure (par une broche GPIO), ce qui
// supprime sa consommation le reste du temps. En veille profonde, plus tard,
// la broche d'alimentation restera à LOW.

#pragma once
#include <Arduino.h>

namespace LightSensor {

  struct Reading {
    uint16_t raw;        // valeur brute moyennée, 0 à 4095
    uint16_t millivolts; // tension mesurée sur la broche S, corrigée par l'étalonnage d'usine
  };

  void begin();
  // Alimente le capteur, attend la stabilisation, moyenne plusieurs lectures, coupe l'alimentation.
  Reading read();
}
