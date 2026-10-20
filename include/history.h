// history.h
// Mémoire des dernières 24 h, pour le téléphone :
//   - la courbe : valeur lissée du capteur à intervalle régulier, avec l'état des rubans ;
//   - le journal : début et fin des dernières soirées.
// Les instants sont comptés en secondes depuis le démarrage de la carte :
// le téléphone les convertit en heures réelles avec sa propre horloge.

#pragma once
#include <Arduino.h>

namespace History {
  uint32_t uptimeSec();
  // Ajoute un point si l'intervalle est écoulé. Renvoie true si un point a été ajouté.
  bool sample(float smoothed, bool interiorOn, bool exteriorOn);
  void eveningStart(float smoothed);
  void eveningEnd();
  uint16_t eveningsCount();
  // Courbe en binaire : en-tête de 8 octets, puis 2 octets par point (du plus ancien au plus récent).
  size_t curveBytes(uint8_t* out, size_t max);
  // Journal en JSON : [[début, fin, valeur], ...], fin = 0 si la soirée est en cours.
  String journalJson();
}
