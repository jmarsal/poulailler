// power_monitor.h
// Mesure de l'accu par un module INA219 (I2C, adresse 0x40, shunt de 0,1 Ω).
//
// Branchement prévu : VIN+ sur OUT+ de la TP4056, VIN− vers le point commun
// (BAT+ du CN3065 et VIN+ du MT3608). Tout le courant de l'accu passe par le shunt :
//   - tension : celle de l'accu (moins la petite chute de la protection) ;
//   - courant : positif quand l'accu se charge, négatif quand il se décharge.
//
// Sans module branché, begin() le dit et le reste du programme fait comme avant.
// Entre deux mesures, l'INA219 est mis en veille (quelques µA au lieu d'environ 1 mA).

#pragma once
#include <Arduino.h>

namespace PowerMonitor {
  bool begin();            // true si l'INA219 répond
  bool present();
  struct Reading { bool ok; uint16_t mv; int16_t ma; };
  Reading read();          // réveille le module, mesure, le rendort
  Reading last();          // dernière mesure (ok = false si aucune)
}
