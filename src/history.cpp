// history.cpp
#include "history.h"
#include "config.h"
#include <esp_attr.h>
#include "clock.h"

namespace {
  // En mémoire RTC : gardé pendant le sommeil profond, effacé à la mise sous tension.
  RTC_DATA_ATTR uint16_t points[HISTORY_POINTS];
  RTC_DATA_ATTR uint16_t count = 0, head = 0;          // head : prochaine case à écrire
  RTC_DATA_ATTR uint32_t lastSampleSec = 0;
  RTC_DATA_ATTR bool haveSample = false;

  struct Evening { uint32_t start, end; uint16_t value; };
  RTC_DATA_ATTR Evening journal[JOURNAL_EVENINGS];
  RTC_DATA_ATTR uint8_t jCount = 0, jHead = 0;
  RTC_DATA_ATTR uint16_t evenings = 0;

  void put16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
  void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (v >> (8 * i)) & 0xFF; }
}

namespace History {

// Secondes depuis la mise sous tension, sommeils profonds compris.
uint32_t uptimeSec() { return Clock::monoSec(); }

bool sample(float smoothed, bool interiorOn, bool exteriorOn) {
  const uint32_t now = uptimeSec();
  if (haveSample && (now - lastSampleSec) * 1000UL < HISTORY_PERIOD_MS) return false;
  haveSample = true;
  lastSampleSec = now;
  uint16_t v = smoothed < 0 ? 0 : smoothed > 4095 ? 4095 : (uint16_t)(smoothed + 0.5f);
  // 12 bits pour la lumière, bit 12 : intérieur allumé, bit 13 : entrée allumée.
  points[head] = v | (interiorOn ? 0x1000 : 0) | (exteriorOn ? 0x2000 : 0);
  head = (head + 1) % HISTORY_POINTS;
  if (count < HISTORY_POINTS) ++count;
  return true;
}

void eveningStart(float smoothed) {
  journal[jHead] = {uptimeSec(), 0, (uint16_t)smoothed};
  jHead = (jHead + 1) % JOURNAL_EVENINGS;
  if (jCount < JOURNAL_EVENINGS) ++jCount;
  ++evenings;
}

void eveningEnd() {
  if (jCount == 0) return;
  Evening& e = journal[(jHead + JOURNAL_EVENINGS - 1) % JOURNAL_EVENINGS];
  if (e.end == 0) e.end = uptimeSec();
}

uint16_t eveningsCount() { return evenings; }

size_t curveBytes(uint8_t* out, size_t max) {
  const size_t need = 8 + 2 * (size_t)count;
  if (max < need) return 0;
  put32(out, lastSampleSec);
  put16(out + 4, (uint16_t)(HISTORY_PERIOD_MS / 1000));
  put16(out + 6, count);
  const uint16_t first = (head + HISTORY_POINTS - count) % HISTORY_POINTS;
  for (uint16_t i = 0; i < count; ++i) put16(out + 8 + 2 * i, points[(first + i) % HISTORY_POINTS]);
  return need;
}

String journalJson() {
  String s = "[";
  const uint8_t first = (jHead + JOURNAL_EVENINGS - jCount) % JOURNAL_EVENINGS;
  for (uint8_t i = 0; i < jCount; ++i) {
    const Evening& e = journal[(first + i) % JOURNAL_EVENINGS];
    if (i) s += ",";
    s += "[" + String(e.start) + "," + String(e.end) + "," + String(e.value) + "]";
  }
  return s + "]";
}

}  // namespace History
