// clock.cpp
// Le temps est compté par la carte elle-même, sans dépendre de l'horloge système :
//   temps écoulé = (temps cumulé avant ce réveil, gardé en mémoire RTC) + millis().
// Avant chaque sommeil, on ajoute au cumul le temps de cet éveil et la durée du
// sommeil programmé. Le démarrage après chaque réveil (quelques dixièmes de
// seconde) n'est pas compté : l'horloge retarde un peu, et le téléphone la
// remet à l'heure à chaque connexion.
#include "clock.h"
#include <sys/time.h>
#include <esp_attr.h>

namespace {
  RTC_DATA_ATTR uint64_t accMs = 0;        // temps écoulé avant ce réveil
  RTC_DATA_ATTR int64_t epochOffsetMs = 0; // heure réelle (ms) - monoMs()
  RTC_DATA_ATTR bool known = false;        // heure réelle reçue du téléphone

  void applyTimeZone() {
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);   // heure de Paris, été comme hiver
    tzset();
  }

  // Recale l'horloge système (pour localtime) sur notre propre compte.
  void syncSystemTime() {
    if (!known) return;
    const int64_t ms = epochOffsetMs + (int64_t)Clock::monoMs();
    struct timeval tv = {(time_t)(ms / 1000), (suseconds_t)((ms % 1000) * 1000)};
    settimeofday(&tv, nullptr);
  }
}

namespace Clock {

void begin(bool coldBoot) {
  if (coldBoot) {
    accMs = 0;
    epochOffsetMs = 0;
    known = false;
  }
  applyTimeZone();   // l'environnement ne survit pas au sommeil profond
  syncSystemTime();
}

uint64_t monoMs() { return accMs + (uint64_t)millis(); }

void beforeSleep(uint32_t sleepMs) { accMs += (uint64_t)millis() + sleepMs; }

void setEpoch(uint32_t e) {
  epochOffsetMs = (int64_t)e * 1000 - (int64_t)monoMs();
  known = true;
  syncSystemTime();
}

bool epochKnown() { return known; }

uint32_t epoch() { return known ? (uint32_t)((epochOffsetMs + (int64_t)monoMs()) / 1000) : 0; }

}  // namespace Clock
