// dusk_controller.h
// Détection du crépuscule et déroulé de l'éclairage du soir, sur deux sorties.
//
// Logique pure : aucun accès au matériel, aucune dépendance à Arduino.
// On lui donne l'heure (millis) et la valeur brute du capteur, elle répond
// par une phase et un niveau d'éclairage de 0.0 à 1.0 pour chaque sortie.
// C'est ce qui permet de la tester sur le Mac (pio test -e native).
//
// Câblage retenu : plus de lumière = valeur plus haute.
//
// Déroulé :
//
//   ATTENTE DU JOUR ──(jour confirmé)──> JOUR ──(crépuscule confirmé)──> SOIRÉE
//         ▲                               ▲                                │
//   (démarrage)                           │                 (toutes les sorties éteintes)
//                                         │                                ▼
//                                         └──(nuit minimale écoulée, puis jour confirmé)── NUIT
//
// La soirée peut aussi être lancée à la main (startEvening), depuis n'importe
// quelle phase.
//
// Pendant la SOIRÉE, chaque sortie suit son propre profil à partir de
// l'instant du déclenchement : montée, pleine lumière, fondu, extinction.
// Réglage actuel : extérieur 35 min puis extinction ; intérieur 30 min de
// pleine lumière puis 30 min de baisse progressive.
//
// Garde-fous :
// - Au démarrage, on attend d'avoir vu le jour : une carte qui redémarre à
//   3 h du matin (accu revenu, coupure) n'allume pas le poulailler.
// - Un seul cycle par soir : le réarmement est verrouillé pendant une durée
//   minimale après le déclenchement. La lumière de nos propres rubans, ou des
//   phares, ne peut donc pas faire croire au retour du jour.
// - Hystérésis : le seuil de jour est nettement au-dessus du seuil de
//   crépuscule, et chaque franchissement doit durer pour être pris en compte.

#pragma once
#include <cstdint>

namespace dusk {

enum class Phase : uint8_t { WaitDay, Day, Evening, Night };
enum class Output : uint8_t { Interior, Exterior };

// Nom lisible d'une phase, pour le moniteur série.
const char* phaseName(Phase p);

// Profil d'une sortie, compté depuis le déclenchement.
struct Profile {
  uint32_t fadeInMs;   // montée de 0 à pleine lumière
  uint32_t holdMs;     // fin de la pleine lumière (montée comprise)
  uint32_t fadeOutMs;  // durée de la baisse jusqu'à l'extinction
  float maxLevel;      // pleine lumière, de 0.0 à 1.0 (pour brider un ruban trop fort)
};

struct Config {
  uint16_t duskThreshold;  // valeur lissée en dessous de laquelle c'est le crépuscule
  uint16_t dayThreshold;   // valeur lissée au-dessus de laquelle c'est le jour (> duskThreshold)
  uint32_t smoothingMs;    // constante de temps du lissage (0 = pas de lissage)
  uint32_t duskConfirmMs;  // durée pendant laquelle il doit rester sombre avant d'allumer
  uint32_t dayConfirmMs;   // durée pendant laquelle il doit rester clair avant de réarmer
  uint32_t minNightMs;     // verrou : délai minimal entre le déclenchement et le réarmement
  Profile interior;
  Profile exterior;
};

class Controller {
 public:
  explicit Controller(const Config& cfg);

  // À appeler à chaque mesure. Renvoie true si la phase a changé.
  bool update(uint32_t nowMs, uint16_t raw);

  // À appeler souvent (dans loop) : termine la soirée entre deux mesures.
  // Renvoie true si la phase a changé.
  bool tick(uint32_t nowMs);

  // Soirée lancée à la main (bouton du téléphone) : démarre le déroulé tout de
  // suite, quelle que soit la phase (une soirée en cours repart de zéro). Ensuite
  // tout se passe comme pour un crépuscule détecté, verrou de nuit compris.
  void startEvening(uint32_t nowMs);

  // Niveau voulu pour une sortie à cet instant, de 0.0 à 1.0.
  float level(Output out, uint32_t nowMs) const;

  // Réglages modifiables en marche (seuils, durées depuis le téléphone).
  // Une soirée en cours suit aussitôt les nouvelles durées.
  void setConfig(const Config& cfg) { cfg_ = cfg; }
  const Config& config() const { return cfg_; }

  Phase phase() const { return phase_; }
  float smoothed() const { return smooth_; }

 private:
  void enter(Phase p, uint32_t nowMs);
  bool confirmed(bool cond, uint32_t nowMs, uint32_t durationMs);
  uint32_t eveningMs() const;   // durée de la soirée : jusqu'à l'extinction de la dernière sortie

  Config cfg_;
  Phase phase_ = Phase::WaitDay;
  uint32_t phaseStart_ = 0;
  uint32_t triggerAt_ = 0;

  bool haveSample_ = false;
  float smooth_ = 0.0f;
  uint32_t lastSample_ = 0;

  bool condActive_ = false;
  uint32_t condSince_ = 0;
};

// Niveau d'un profil, `elapsed` millisecondes après le déclenchement.
float profileLevel(const Profile& p, uint32_t elapsed);

}  // namespace dusk
