// clock.cpp
#include "clock.h"
#include <sys/time.h>
#include <esp_attr.h>

namespace {
  // Gardées pendant le sommeil profond (mémoire RTC), remises à zéro à la mise sous tension.
  RTC_DATA_ATTR int64_t baseUs = 0;     // instant système correspondant à monoMs() = 0
  RTC_DATA_ATTR bool known = false;     // heure réelle reçue du téléphone

  int64_t nowUs() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
  }

  void applyTimeZone() {
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);   // heure de Paris, été comme hiver
    tzset();
  }
}

namespace Clock {

void begin(bool coldBoot) {
  if (coldBoot) {
    baseUs = nowUs();
    known = false;
  }
  applyTimeZone();   // l'environnement ne survit pas au sommeil profond
}

uint64_t monoMs() {
  const int64_t d = nowUs() - baseUs;
  return d > 0 ? (uint64_t)(d / 1000) : 0;
}

void setEpoch(uint32_t e) {
  const int64_t before = nowUs();
  struct timeval tv = {(time_t)e, 0};
  settimeofday(&tv, nullptr);
  // L'horloge système vient de sauter : on décale la base d'autant, monoMs() ne bouge pas.
  baseUs += nowUs() - before;
  known = true;
  applyTimeZone();
}

bool epochKnown() { return known; }

uint32_t epoch() { return known ? (uint32_t)time(nullptr) : 0; }

}  // namespace Clock
