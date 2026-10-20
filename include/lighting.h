// lighting.h
// Commande des deux rubans LED par PWM, chacun à travers son module MOSFET D4184.
// PWM (modulation de largeur d'impulsion) : la broche bascule très vite entre 0 et 3,3 V ;
// la part du temps passée à 3,3 V (le rapport cyclique) règle la luminosité perçue.

#pragma once
#include <Arduino.h>
#include "dusk_controller.h"

namespace Lighting {
  void begin();
  // Luminosité de 0.0 (éteint) à 1.0 (plein), avec correction gamma pour un fondu régulier à l'œil.
  void setLevel(dusk::Output out, float level);
  // Banc d'essai : fait monter puis descendre les deux sorties en continu. À appeler dans loop().
  void updateFadeTest();
}
