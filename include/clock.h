// clock.h
// Horloges qui survivent au sommeil profond.
//
// En sommeil profond, la carte redémarre à chaque réveil : millis() repart de
// zéro. On compte donc le temps avec l'horloge système (gettimeofday), que
// l'ESP32 entretient pendant le sommeil grâce à son horloge basse consommation.
//
// - monoMs() : temps écoulé depuis la dernière mise sous tension, sans saut,
//   même quand le téléphone remet l'heure. Sert à toute la logique (lissage,
//   confirmations, durées de la soirée).
// - epoch() : heure réelle (secondes depuis 1970), 0 tant que le téléphone ne
//   l'a pas donnée depuis la mise sous tension.
//
// L'horloge de veille dérive de quelques minutes par jour : le téléphone la
// remet à l'heure à chaque connexion, et l'application corrige les mesures.

#pragma once
#include <Arduino.h>

namespace Clock {
  // À appeler au début de setup(). coldBoot : mise sous tension ou redémarrage
  // (pas un réveil de sommeil profond).
  void begin(bool coldBoot);
  uint64_t monoMs();
  inline uint32_t monoSec() { return (uint32_t)(monoMs() / 1000); }
  // Heure réelle donnée par le téléphone (secondes, UTC).
  void setEpoch(uint32_t epoch);
  bool epochKnown();
  uint32_t epoch();
}
