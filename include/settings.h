// settings.h
// Réglages modifiables depuis le téléphone, gardés en mémoire flash (NVS) :
// ils survivent aux coupures. En l'absence de réglage enregistré, ce sont
// les valeurs de config.h qui s'appliquent.
//
// Uniquement en mode réel : en mode test (FAST_TEST), les durées sont en
// secondes et les réglages enregistrés sont ignorés.

#pragma once
#include <Arduino.h>
#include "dusk_controller.h"

namespace Settings {
  // Réglages de départ : config.h, puis ce qui a été enregistré.
  dusk::Config load();
  // Traite « set <nom> <valeur> » ou « reset ». Met à jour `cfg` si accepté.
  // Renvoie un message court pour le téléphone et le moniteur.
  String apply(const String& cmd, dusk::Config& cfg);
  // Réglages courants au format JSON (seuils, minutes, pourcentages).
  String toJson(const dusk::Config& cfg);
}
