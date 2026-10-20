// Tests de la détection du crépuscule, exécutés sur le Mac :
//   pio test -e native
// Aucune carte nécessaire : on simule le temps et les mesures.

#include <unity.h>
#include "dusk_controller.h"

using dusk::Controller;
using dusk::Output;
using dusk::Phase;

namespace {
// Réglages de test en millisecondes « symboliques », sans lissage pour des résultats exacts.
// Même forme que le réglage réel : extérieur plein puis extinction rapide,
// intérieur plein pendant la même durée puis longue baisse.
constexpr dusk::Config CFG = {
  /* duskThreshold */ 1000,
  /* dayThreshold  */ 2500,
  /* smoothingMs   */ 0,
  /* duskConfirmMs */ 100,
  /* dayConfirmMs  */ 100,
  /* minNightMs    */ 2000,
  /* interior      */ {10, 300, 300, 1.0f},
  /* exterior      */ {10, 300, 10, 1.0f},
};
constexpr uint16_t DARK = 200;
constexpr uint16_t DIM = 1800;    // entre les deux seuils
constexpr uint16_t BRIGHT = 4000;
constexpr uint32_t STEP = 10;     // une mesure toutes les 10 ms

// Envoie la même valeur pendant `duration`, renvoie l'heure atteinte.
uint32_t feed(Controller& c, uint32_t t, uint16_t raw, uint32_t duration) {
  // Boucle sur la durée, pas sur l'heure de fin : reste juste au débordement de millis().
  for (uint32_t done = 0; done < duration; done += STEP, t += STEP) c.update(t, raw);
  return t;
}

// Amène le contrôleur en phase JOUR.
uint32_t reachDay(Controller& c) {
  uint32_t t = feed(c, 0, BRIGHT, 200);
  TEST_ASSERT_EQUAL(Phase::Day, c.phase());
  return t;
}

// Fait tomber la nuit mesure par mesure ; renvoie l'instant exact du déclenchement.
uint32_t trigger(Controller& c, uint32_t t) {
  while (c.phase() == Phase::Day) { c.update(t, DARK); t += STEP; }
  TEST_ASSERT_EQUAL(Phase::Evening, c.phase());
  return t - STEP;
}
}  // namespace

void test_demarrage_de_nuit_n_allume_pas() {
  Controller c(CFG);
  feed(c, 0, DARK, 5000);
  TEST_ASSERT_EQUAL(Phase::WaitDay, c.phase());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.level(Output::Interior, 5000));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.level(Output::Exterior, 5000));
}

void test_le_jour_arme_le_controleur() {
  Controller c(CFG);
  reachDay(c);
}

void test_une_ombre_breve_ne_declenche_pas() {
  Controller c(CFG);
  uint32_t t = reachDay(c);
  t = feed(c, t, DARK, 50);       // moitié de la durée de confirmation
  t = feed(c, t, BRIGHT, 50);
  t = feed(c, t, DARK, 50);       // le compteur doit être reparti de zéro
  TEST_ASSERT_EQUAL(Phase::Day, c.phase());
}

void test_entre_les_deux_seuils_rien_ne_bouge() {
  Controller c(CFG);
  uint32_t t = reachDay(c);
  feed(c, t, DIM, 5000);
  TEST_ASSERT_EQUAL(Phase::Day, c.phase());
}

void test_crepuscule_confirme_allume() {
  Controller c(CFG);
  uint32_t t = reachDay(c);
  t = feed(c, t, DARK, 90);
  TEST_ASSERT_EQUAL(Phase::Day, c.phase());
  t = feed(c, t, DARK, 20);
  TEST_ASSERT_EQUAL(Phase::Evening, c.phase());
}

void test_profil_exterieur() {
  Controller c(CFG);
  const uint32_t on = trigger(c, reachDay(c));
  TEST_ASSERT_EQUAL_FLOAT(0.5f, c.level(Output::Exterior, on + 5));    // montée
  TEST_ASSERT_EQUAL_FLOAT(1.0f, c.level(Output::Exterior, on + 299));  // fin du plein
  TEST_ASSERT_EQUAL_FLOAT(0.5f, c.level(Output::Exterior, on + 305));  // extinction rapide
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.level(Output::Exterior, on + 310));
}

void test_profil_interieur_plein_puis_baisse() {
  Controller c(CFG);
  const uint32_t on = trigger(c, reachDay(c));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, c.level(Output::Interior, on + 299));  // plein comme l'extérieur
  TEST_ASSERT_EQUAL_FLOAT(1.0f, c.level(Output::Interior, on + 300));  // début de la baisse
  TEST_ASSERT_EQUAL_FLOAT(0.5f, c.level(Output::Interior, on + 450));  // mi-baisse
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.level(Output::Exterior, on + 450));  // extérieur déjà éteint
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.level(Output::Interior, on + 600));
}

void test_la_soiree_dure_jusqu_a_la_derniere_sortie() {
  Controller c(CFG);
  const uint32_t on = trigger(c, reachDay(c));
  c.tick(on + 400);                                    // extérieur éteint, intérieur encore allumé
  TEST_ASSERT_EQUAL(Phase::Evening, c.phase());
  c.tick(on + 600);
  TEST_ASSERT_EQUAL(Phase::Night, c.phase());
}

void test_brider_une_sortie() {
  dusk::Config cfg = CFG;
  cfg.interior.maxLevel = 0.6f;
  Controller c(cfg);
  const uint32_t on = trigger(c, reachDay(c));
  TEST_ASSERT_EQUAL_FLOAT(0.6f, c.level(Output::Interior, on + 200));
  TEST_ASSERT_EQUAL_FLOAT(0.3f, c.level(Output::Interior, on + 450));
}

void test_nos_propres_lumieres_ne_rearment_pas() {
  Controller c(CFG);
  uint32_t t = trigger(c, reachDay(c)) + STEP;
  // Pendant toute la soirée et après, le ruban de l'entrée éclaire le capteur.
  t = feed(c, t, BRIGHT, 1800);
  TEST_ASSERT_EQUAL(Phase::Night, c.phase());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.level(Output::Interior, t));
}

void test_le_matin_rearme_et_le_soir_suivant_rallume() {
  Controller c(CFG);
  uint32_t t = trigger(c, reachDay(c)) + STEP;
  t = feed(c, t, DARK, 3000);                 // la nuit
  TEST_ASSERT_EQUAL(Phase::Night, c.phase());
  t = feed(c, t, BRIGHT, 200);                // le matin
  TEST_ASSERT_EQUAL(Phase::Day, c.phase());
  t = feed(c, t, DARK, 200);                  // le soir suivant
  TEST_ASSERT_EQUAL(Phase::Evening, c.phase());
}

void test_le_lissage_amortit_un_eclair() {
  dusk::Config cfg = CFG;
  cfg.smoothingMs = 200;
  Controller c(cfg);
  uint32_t t = feed(c, 0, DARK, 500);
  c.update(t, BRIGHT);                        // un flash de phares sur une mesure
  TEST_ASSERT_TRUE(c.smoothed() < 500.0f);
}

void test_millis_qui_repasse_par_zero() {
  Controller c(CFG);
  uint32_t t = 0xFFFFFFFFu - 150;             // juste avant le débordement (49 jours)
  t = feed(c, t, BRIGHT, 200);
  TEST_ASSERT_EQUAL(Phase::Day, c.phase());
  t = feed(c, t, DARK, 200);
  TEST_ASSERT_EQUAL(Phase::Evening, c.phase());
  TEST_ASSERT_TRUE(c.level(Output::Interior, t) > 0.9f);
}

void test_changer_le_seuil_en_marche() {
  Controller c(CFG);
  uint32_t t = reachDay(c);
  t = feed(c, t, DIM, 300);                   // entre les deux seuils : rien
  TEST_ASSERT_EQUAL(Phase::Day, c.phase());
  dusk::Config cfg = CFG;
  cfg.duskThreshold = 2000;                   // seuil relevé depuis le téléphone
  c.setConfig(cfg);
  t = feed(c, t, DIM, 300);                   // la même lumière déclenche désormais
  TEST_ASSERT_EQUAL(Phase::Evening, c.phase());
  TEST_ASSERT_EQUAL(2000, c.config().duskThreshold);
}

void test_soiree_manuelle_en_plein_jour() {
  Controller c(CFG);
  uint32_t t = reachDay(c);
  t = feed(c, t, DIM, 100);                   // pas encore assez sombre
  c.startEvening(t);                          // la porte est fermée : bouton du téléphone
  TEST_ASSERT_EQUAL(Phase::Evening, c.phase());
  TEST_ASSERT_EQUAL_FLOAT(1.0f, c.level(Output::Exterior, t + 100));
  t = feed(c, t, DIM, 700);                   // le déroulé se termine normalement
  TEST_ASSERT_EQUAL(Phase::Night, c.phase());
  t = feed(c, t, DARK, 500);                  // pas de deuxième soirée le même soir
  TEST_ASSERT_EQUAL(Phase::Night, c.phase());
}

void test_soiree_manuelle_relancee_apres_la_nuit() {
  Controller c(CFG);
  uint32_t t = trigger(c, reachDay(c)) + STEP;
  t = feed(c, t, DARK, 700);
  TEST_ASSERT_EQUAL(Phase::Night, c.phase());
  c.startEvening(t);
  TEST_ASSERT_EQUAL(Phase::Evening, c.phase());
  TEST_ASSERT_TRUE(c.level(Output::Interior, t + 50) > 0.9f);
}

void test_soiree_manuelle_apres_un_redemarrage() {
  Controller c(CFG);
  uint32_t t = feed(c, 0, DARK, 500);         // redémarrée de nuit : attente du jour
  TEST_ASSERT_EQUAL(Phase::WaitDay, c.phase());
  c.startEvening(t);
  TEST_ASSERT_EQUAL(Phase::Evening, c.phase());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_demarrage_de_nuit_n_allume_pas);
  RUN_TEST(test_le_jour_arme_le_controleur);
  RUN_TEST(test_une_ombre_breve_ne_declenche_pas);
  RUN_TEST(test_entre_les_deux_seuils_rien_ne_bouge);
  RUN_TEST(test_crepuscule_confirme_allume);
  RUN_TEST(test_profil_exterieur);
  RUN_TEST(test_profil_interieur_plein_puis_baisse);
  RUN_TEST(test_la_soiree_dure_jusqu_a_la_derniere_sortie);
  RUN_TEST(test_brider_une_sortie);
  RUN_TEST(test_nos_propres_lumieres_ne_rearment_pas);
  RUN_TEST(test_le_matin_rearme_et_le_soir_suivant_rallume);
  RUN_TEST(test_le_lissage_amortit_un_eclair);
  RUN_TEST(test_millis_qui_repasse_par_zero);
  RUN_TEST(test_changer_le_seuil_en_marche);
  RUN_TEST(test_soiree_manuelle_en_plein_jour);
  RUN_TEST(test_soiree_manuelle_relancee_apres_la_nuit);
  RUN_TEST(test_soiree_manuelle_apres_un_redemarrage);
  return UNITY_END();
}
