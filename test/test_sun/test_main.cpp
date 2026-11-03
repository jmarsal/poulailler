// Tests du calcul du coucher du soleil, exécutés sur le Mac :
//   pio test -e native
// Les valeurs de référence viennent d'une bibliothèque indépendante (astral,
// en Python), pour ne pas tester le calcul contre lui-même.

#include <unity.h>
#include <cstdio>
#include <cstdlib>
#include "sun.h"

namespace {
constexpr double LAT = 48.95, LON = 1.25;   // poulailler, près d'Évreux
constexpr int32_t TOLERANCE = 120;          // 2 min

struct Ref { uint32_t noonUtc; uint32_t sunsetUtc; };
const Ref REFS[] = {
  {1767268800u, 1767283687u},  // 2026-01-01 : coucher 16:08 UTC
  {1771156800u, 1771175711u},  // 2026-02-15 : coucher 17:15 UTC
  {1774008000u, 1774030045u},  // 2026-03-20 : coucher 18:07 UTC (équinoxe)
  {1774785600u, 1774808462u},  // 2026-03-29 : coucher 18:21 UTC (passage à l'heure d'été)
  {1777636800u, 1777662621u},  // 2026-05-01 : coucher 19:10 UTC
  {1782043200u, 1782072144u},  // 2026-06-21 : coucher 20:02 UTC (solstice d'été)
  {1786795200u, 1786821026u},  // 2026-08-15 : coucher 19:10 UTC
  {1790164800u, 1790185847u},  // 2026-09-23 : coucher 17:50 UTC
  {1792929600u, 1792946774u},  // 2026-10-25 : coucher 16:46 UTC (passage à l'heure d'hiver)
  {1793707200u, 1793723457u},  // 2026-11-03 : coucher 16:30 UTC, soit 17:30 à Évreux
  {1797854400u, 1797868783u},  // 2026-12-21 : coucher 15:59 UTC (solstice d'hiver)
  {1803816000u, 1803836156u},  // 2027-02-28 : coucher 17:35 UTC
  {1835438400u, 1835458629u},  // 2028-02-29 : coucher 17:37 UTC (année bissextile)
};
}  // namespace

void test_coucher_toute_l_annee_a_deux_minutes_pres() {
  for (const Ref& r : REFS) {
    const int32_t diff = (int32_t)(sun::sunsetUtc(r.noonUtc, LAT, LON) - r.sunsetUtc);
    char msg[48];
    snprintf(msg, sizeof(msg), "jour %u : écart %d s", (unsigned)r.noonUtc, (int)diff);
    TEST_ASSERT_TRUE_MESSAGE(std::abs(diff) <= TOLERANCE, msg);
  }
}

void test_meme_resultat_quelle_que_soit_l_heure_du_jour() {
  // Le résultat ne dépend que du jour UTC : 0 h 00, midi et 23 h 59 donnent le même coucher.
  const uint32_t noon = 1793707200u;                 // 3 novembre 2026, midi UTC
  const uint32_t a = sun::sunsetUtc(noon - 12 * 3600, LAT, LON);
  const uint32_t b = sun::sunsetUtc(noon, LAT, LON);
  const uint32_t c = sun::sunsetUtc(noon + 12 * 3600 - 1, LAT, LON);
  TEST_ASSERT_EQUAL_UINT32(b, a);
  TEST_ASSERT_EQUAL_UINT32(b, c);
}

void test_le_coucher_avance_de_jour_en_jour_en_automne() {
  const uint32_t d1 = sun::sunsetUtc(1793707200u, LAT, LON);          // 3 novembre
  const uint32_t d2 = sun::sunsetUtc(1793707200u + 86400, LAT, LON);  // 4 novembre
  const int32_t step = (int32_t)(d2 - d1) - 86400;                    // décalage d'un jour à l'autre
  TEST_ASSERT_TRUE(step < 0 && step > -180);                          // environ une minute et demie plus tôt
}

void test_pas_de_coucher_en_nuit_polaire() {
  // Cap Nord le 21 décembre : le soleil ne se lève pas.
  TEST_ASSERT_EQUAL_UINT32(0, sun::sunsetUtc(1797854400u, 71.17, 25.78));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_coucher_toute_l_annee_a_deux_minutes_pres);
  RUN_TEST(test_meme_resultat_quelle_que_soit_l_heure_du_jour);
  RUN_TEST(test_le_coucher_avance_de_jour_en_jour_en_automne);
  RUN_TEST(test_pas_de_coucher_en_nuit_polaire);
  return UNITY_END();
}
