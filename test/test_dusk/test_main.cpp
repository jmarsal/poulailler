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

// ---------------------------------------------------------------------------
// Règles calées sur le coucher du soleil, avec des durées réelles et une
// mesure toutes les 30 s, comme sur la carte.
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t MIN = 60000;
constexpr dusk::Config REAL = {
  /* duskThreshold */ 1000,
  /* dayThreshold  */ 2500,
  /* smoothingMs   */ 0,
  /* duskConfirmMs */ 10 * MIN,
  /* dayConfirmMs  */ 15 * MIN,
  /* minNightMs    */ 8 * 60 * MIN,
  /* interior      */ {5000, 30 * MIN, 30 * MIN, 1.0f},
  /* exterior      */ {5000, 35 * MIN, 10000, 1.0f},
};
//                              fenêtre, secours, fin, entrée, intérieur, plafond
constexpr dusk::SolarConfig SOLAR = {true, 20, 15, 60, 30, 30, 60};
constexpr uint32_t SUNSET = 10 * 60 * MIN;    // coucher du soleil à t = 10 h de fonctionnement
constexpr uint32_t TICK = 30000;              // une mesure toutes les 30 s

dusk::SunClock sunAt(uint32_t t, bool known = true) {
  return dusk::SunClock{known, (int32_t)(((int64_t)t - (int64_t)SUNSET) / 1000)};
}
uint32_t at(int32_t minFromSunset) { return (uint32_t)((int64_t)SUNSET + (int64_t)minFromSunset * MIN); }

// Envoie la même lumière de `from` à `to` (minutes par rapport au coucher).
// S'arrête dès que la soirée démarre et renvoie l'instant du déclenchement (0 sinon).
uint32_t sim(Controller& c, int32_t from, int32_t to, uint16_t raw, bool known = true) {
  for (uint32_t t = at(from); t < at(to); t += TICK) {
    const bool before = c.phase() == Phase::Evening;
    c.update(t, raw, sunAt(t, known));
    if (!before && c.phase() == Phase::Evening) return t;
  }
  return 0;
}

Controller armed(int32_t from = -300) {
  Controller c(REAL);
  c.setSolar(SOLAR);
  sim(c, from, from + 20, BRIGHT);            // 20 min de plein jour : armée
  TEST_ASSERT_EQUAL(Phase::Day, c.phase());
  return c;
}
}  // namespace

void test_soleil_un_orage_avant_la_fenetre_ne_declenche_pas() {
  Controller c = armed();
  TEST_ASSERT_EQUAL_UINT32(0, sim(c, -150, -60, DARK));   // 1 h 30 de noir, 1 h avant le coucher
  TEST_ASSERT_EQUAL(Phase::Day, c.phase());
  sim(c, -60, -40, BRIGHT);                                // l'orage passe
  TEST_ASSERT_EQUAL(Phase::Day, c.phase());
}

void test_soleil_un_noir_qui_dure_part_a_l_ouverture_de_la_fenetre() {
  Controller c = armed();
  const uint32_t on = sim(c, -60, 0, DARK);
  TEST_ASSERT_EQUAL_UINT32(at(-20), on);                   // pile à coucher - 20 min
}

void test_soleil_dans_la_fenetre_le_seuil_decide_comme_avant() {
  Controller c = armed();
  sim(c, -280, -15, BRIGHT);
  const uint32_t on = sim(c, -15, 30, DARK);               // nuit qui tombe à coucher - 15
  TEST_ASSERT_EQUAL_UINT32(at(-5), on);                    // 10 min de confirmation
}

void test_soleil_heure_inconnue_la_lumiere_decide_seule() {
  Controller c = armed();
  const uint32_t on = sim(c, -150, -60, DARK, false);
  TEST_ASSERT_EQUAL_UINT32(at(-140), on);                  // comportement d'origine
}

void test_soleil_secours_si_le_capteur_ne_voit_rien() {
  Controller c = armed();
  const uint32_t on = sim(c, -280, 120, BRIGHT);           // capteur ébloui ou en panne
  TEST_ASSERT_EQUAL_UINT32(at(15), on);                    // coucher + 15 min
}

void test_soleil_pas_de_secours_apres_la_fenetre() {
  Controller c = armed(70);                                // armée seulement à coucher + 90
  TEST_ASSERT_EQUAL_UINT32(0, sim(c, 90, 240, BRIGHT));
  TEST_ASSERT_EQUAL(Phase::Day, c.phase());
}

void test_soleil_l_entree_tient_jusqu_a_coucher_plus_30() {
  Controller c = armed();
  const uint32_t on = sim(c, -60, 0, DARK);                // déclenchée à coucher - 20
  // 35 min d'origine finiraient à coucher + 15 : allongée de 15 min.
  TEST_ASSERT_EQUAL_UINT32(15 * MIN, c.extensionMs(Output::Exterior));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, c.level(Output::Exterior, at(29)));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.level(Output::Exterior, at(31)));
  TEST_ASSERT_EQUAL_UINT32(at(-20), on);
}

void test_soleil_l_interieur_reste_plein_jusqu_a_coucher_plus_30() {
  Controller c = armed();
  sim(c, -60, 0, DARK);                                    // déclenchée à coucher - 20
  TEST_ASSERT_EQUAL_UINT32(20 * MIN, c.extensionMs(Output::Interior));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, c.level(Output::Interior, at(29)));
  TEST_ASSERT_EQUAL_FLOAT(0.5f, c.level(Output::Interior, at(45)));   // mi-baisse
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.level(Output::Interior, at(61)));
}

void test_soleil_pas_d_allongement_si_la_soiree_part_tard() {
  Controller c = armed();
  sim(c, -280, 0, BRIGHT);
  sim(c, 0, 30, DARK);                                     // déclenchée à coucher + 10
  TEST_ASSERT_EQUAL_UINT32(0, c.extensionMs(Output::Exterior));
  TEST_ASSERT_EQUAL_UINT32(0, c.extensionMs(Output::Interior));
}

void test_soleil_allongement_plafonne() {
  Controller c(REAL);
  dusk::SolarConfig s = SOLAR;
  s.windowBeforeMin = 240;                                 // fenêtre très large pour ce test
  c.setSolar(s);
  sim(c, -300, -280, BRIGHT);
  sim(c, -160, -100, DARK);                                // déclenchée à coucher - 150
  TEST_ASSERT_EQUAL_UINT32(60 * MIN, c.extensionMs(Output::Exterior));
  TEST_ASSERT_EQUAL_UINT32(60 * MIN, c.extensionMs(Output::Interior));
}

void test_soleil_la_soiree_dure_jusqu_a_la_fin_de_l_allongement() {
  Controller c = armed();
  sim(c, -60, 0, DARK);                                    // déclenchée à coucher - 20
  c.tick(at(59));
  TEST_ASSERT_EQUAL(Phase::Evening, c.phase());            // intérieur en baisse jusqu'à coucher + 60
  c.tick(at(61));
  TEST_ASSERT_EQUAL(Phase::Night, c.phase());
}

void test_soleil_soiree_manuelle_sans_allongement() {
  Controller c = armed();
  c.startEvening(at(-120));
  TEST_ASSERT_EQUAL(Phase::Evening, c.phase());
  TEST_ASSERT_EQUAL_UINT32(0, c.extensionMs(Output::Exterior));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.level(Output::Exterior, at(-120) + 36 * MIN));
}

void test_soleil_desactive_par_defaut() {
  Controller c(REAL);                                      // pas de setSolar
  sim(c, -300, -280, BRIGHT);
  const uint32_t on = sim(c, -150, -60, DARK);
  TEST_ASSERT_EQUAL_UINT32(at(-140), on);
  TEST_ASSERT_EQUAL_UINT32(0, c.extensionMs(Output::Exterior));
}

// Entrée retardée : allumée seulement à partir de coucher + 10, tenue jusqu'à
// coucher + 60 (réglages de la version k).
namespace {
constexpr dusk::SolarConfig SOLAR_K = {true, 20, 15, 60, 60, 30, 60, 10};
Controller armedK() {
  Controller c(REAL);
  c.setSolar(SOLAR_K);
  sim(c, -300, -280, BRIGHT);
  TEST_ASSERT_EQUAL(Phase::Day, c.phase());
  return c;
}
}  // namespace

void test_entree_retardee_s_allume_a_coucher_plus_10() {
  Controller c = armedK();
  TEST_ASSERT_EQUAL_UINT32(at(-20), sim(c, -60, 0, DARK)); // soirée à coucher - 20
  TEST_ASSERT_EQUAL_UINT32(30 * MIN, c.delayMs(Output::Exterior));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.level(Output::Exterior, at(9)));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, c.level(Output::Exterior, at(11)));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, c.level(Output::Interior, at(-19)));  // l'intérieur, lui, tout de suite
}

void test_entree_retardee_tient_jusqu_a_coucher_plus_60() {
  Controller c = armedK();
  sim(c, -60, 0, DARK);                                    // soirée à coucher - 20
  // allumée à + 10, 35 min la mèneraient à + 45 : allongée de 15 min.
  TEST_ASSERT_EQUAL_UINT32(15 * MIN, c.extensionMs(Output::Exterior));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, c.level(Output::Exterior, at(59)));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c.level(Output::Exterior, at(61)));
  c.tick(at(59));
  TEST_ASSERT_EQUAL(Phase::Evening, c.phase());
  c.tick(at(61));
  TEST_ASSERT_EQUAL(Phase::Night, c.phase());
}

void test_entree_retardee_soiree_tardive_allume_aussitot() {
  Controller c = armedK();
  sim(c, -280, 5, BRIGHT);
  const uint32_t on = sim(c, 5, 60, DARK);                 // noir à + 5, soirée à + 15
  TEST_ASSERT_EQUAL_UINT32(at(15), on);
  TEST_ASSERT_EQUAL_UINT32(0, c.delayMs(Output::Exterior));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, c.level(Output::Exterior, at(15) + 10000));
  TEST_ASSERT_EQUAL_UINT32(10 * MIN, c.extensionMs(Output::Exterior));   // 35 + 10 : jusqu'à + 60
}

void test_entree_retardee_heure_inconnue_allume_aussitot() {
  Controller c = armedK();
  const uint32_t on = sim(c, -60, 0, DARK, false);         // sans heure : pas de retard
  TEST_ASSERT_EQUAL_UINT32(0, c.delayMs(Output::Exterior));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, c.level(Output::Exterior, on + 10000));
}

void test_entree_retardee_soiree_manuelle_allume_aussitot() {
  Controller c = armedK();
  c.startEvening(at(-30));
  TEST_ASSERT_EQUAL_UINT32(0, c.delayMs(Output::Exterior));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, c.level(Output::Exterior, at(-30) + 10000));
}

void test_entree_retardee_zero_allume_au_declenchement() {
  Controller c(REAL);
  dusk::SolarConfig s = SOLAR_K;
  s.exteriorStartMin = 0;
  c.setSolar(s);
  sim(c, -300, -280, BRIGHT);
  const uint32_t on = sim(c, -60, 0, DARK);
  TEST_ASSERT_EQUAL_UINT32(0, c.delayMs(Output::Exterior));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, c.level(Output::Exterior, on + 10000));
}

void test_entree_retardee_la_soiree_attend_l_extinction_de_l_entree() {
  Controller c(REAL);
  dusk::SolarConfig s = SOLAR_K;
  s.interiorFullUntilMin = 0;                              // intérieur éteint vers coucher + 40
  c.setSolar(s);
  sim(c, -300, -280, BRIGHT);
  sim(c, -60, 0, DARK);                                    // soirée à coucher - 20
  c.tick(at(50));
  TEST_ASSERT_EQUAL(Phase::Evening, c.phase());            // l'entrée tient jusqu'à + 60
  c.tick(at(61));
  TEST_ASSERT_EQUAL(Phase::Night, c.phase());
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
  RUN_TEST(test_soleil_un_orage_avant_la_fenetre_ne_declenche_pas);
  RUN_TEST(test_soleil_un_noir_qui_dure_part_a_l_ouverture_de_la_fenetre);
  RUN_TEST(test_soleil_dans_la_fenetre_le_seuil_decide_comme_avant);
  RUN_TEST(test_soleil_heure_inconnue_la_lumiere_decide_seule);
  RUN_TEST(test_soleil_secours_si_le_capteur_ne_voit_rien);
  RUN_TEST(test_soleil_pas_de_secours_apres_la_fenetre);
  RUN_TEST(test_soleil_l_entree_tient_jusqu_a_coucher_plus_30);
  RUN_TEST(test_soleil_l_interieur_reste_plein_jusqu_a_coucher_plus_30);
  RUN_TEST(test_soleil_pas_d_allongement_si_la_soiree_part_tard);
  RUN_TEST(test_soleil_allongement_plafonne);
  RUN_TEST(test_soleil_la_soiree_dure_jusqu_a_la_fin_de_l_allongement);
  RUN_TEST(test_soleil_soiree_manuelle_sans_allongement);
  RUN_TEST(test_soleil_desactive_par_defaut);
  RUN_TEST(test_entree_retardee_s_allume_a_coucher_plus_10);
  RUN_TEST(test_entree_retardee_tient_jusqu_a_coucher_plus_60);
  RUN_TEST(test_entree_retardee_soiree_tardive_allume_aussitot);
  RUN_TEST(test_entree_retardee_heure_inconnue_allume_aussitot);
  RUN_TEST(test_entree_retardee_soiree_manuelle_allume_aussitot);
  RUN_TEST(test_entree_retardee_zero_allume_au_declenchement);
  RUN_TEST(test_entree_retardee_la_soiree_attend_l_extinction_de_l_entree);
  return UNITY_END();
}
