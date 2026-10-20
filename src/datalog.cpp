// datalog.cpp
#include "datalog.h"
#include "config.h"
#include <LittleFS.h>

namespace {
  constexpr const char* DIR = "/log";

  bool mounted = false;
  uint16_t boot = 0;

  // Fichiers /log/<n>.bin, LOG_FILE_RECORDS mesures chacun. La mesure numéro s
  // est dans le fichier s / LOG_FILE_RECORDS, à la position s % LOG_FILE_RECORDS.
  uint32_t firstFile = 0, lastFile = 0;
  uint32_t lastCount = 0;        // mesures déjà écrites dans le dernier fichier

  DataLog::Record pending[LOG_BUFFER_RECORDS];
  uint16_t pendingCount = 0;

  // Le téléphone lit depuis la tâche Bluetooth, loop() écrit : un verrou partagé.
  SemaphoreHandle_t lock = nullptr;
  struct Guard {
    Guard()  { if (lock) xSemaphoreTake(lock, portMAX_DELAY); }
    ~Guard() { if (lock) xSemaphoreGive(lock); }
  };

  String fileName(uint32_t n) { return String(DIR) + "/" + String(n) + ".bin"; }
  uint32_t flashEnd() { return lastFile * LOG_FILE_RECORDS + lastCount; }

  void put16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
  void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (v >> (8 * i)) & 0xFF; }

  // Écrit les mesures en attente. Appelé sous verrou.
  void flushLocked() {
    if (!mounted || pendingCount == 0) return;
    uint16_t done = 0;
    while (done < pendingCount) {
      if (lastCount >= LOG_FILE_RECORDS) {          // fichier plein : on passe au suivant
        ++lastFile;
        lastCount = 0;
        while (lastFile - firstFile + 1 > LOG_FILES) {   // le plus ancien est effacé
          LittleFS.remove(fileName(firstFile));
          ++firstFile;
        }
      }
      const uint16_t room = LOG_FILE_RECORDS - lastCount;
      const uint16_t n = min<uint16_t>(room, pendingCount - done);
      File f = LittleFS.open(fileName(lastFile), FILE_APPEND);
      if (!f) { Serial.println(F("journal : écriture impossible")); return; }
      const size_t want = n * sizeof(DataLog::Record);
      const size_t wrote = f.write((const uint8_t*)&pending[done], want);
      f.close();
      if (wrote != want) { Serial.println(F("journal : écriture incomplète")); return; }
      lastCount += n;
      done += n;
    }
    pendingCount = 0;
  }
}

namespace DataLog {

bool begin(uint16_t bootId) {
  boot = bootId;
  if (!lock) lock = xSemaphoreCreateMutex();
  // Au tout premier démarrage, la zone est vierge : LittleFS la formate (quelques secondes).
  mounted = LittleFS.begin(true, "/littlefs", 4, "spiffs");
  if (!mounted) { Serial.println(F("journal : stockage indisponible")); return false; }
  if (!LittleFS.exists(DIR)) LittleFS.mkdir(DIR);

  // Retrouve les fichiers existants : le plus ancien et le plus récent.
  bool any = false;
  uint32_t lo = 0, hi = 0;
  File dir = LittleFS.open(DIR);
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String name = f.name();                       // « 12.bin » (ou chemin complet selon la version)
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    if (!name.endsWith(".bin")) continue;
    const uint32_t n = name.substring(0, name.length() - 4).toInt();
    if (!any || n < lo) lo = n;
    if (!any || n > hi) hi = n;
    any = true;
  }
  if (any) {
    firstFile = lo;
    lastFile = hi;
    File f = LittleFS.open(fileName(hi), FILE_READ);
    const size_t size = f ? f.size() : 0;
    if (f) f.close();
    lastCount = size / sizeof(Record);
    if (size % sizeof(Record)) {                  // fin de fichier abîmée : on repart sur un neuf
      ++lastFile;
      lastCount = 0;
    }
  }
  Serial.printf("journal : %lu mesures gardées (n° %lu à %lu), %u ko utilisés sur %u\n",
                (unsigned long)(flashEnd() - firstFile * LOG_FILE_RECORDS),
                (unsigned long)(firstFile * LOG_FILE_RECORDS), (unsigned long)flashEnd(),
                (unsigned)(LittleFS.usedBytes() / 1024), (unsigned)(LittleFS.totalBytes() / 1024));
  return true;
}

bool ok() { return mounted; }

void add(uint32_t epoch, uint32_t up, uint16_t packed, uint16_t mv, int16_t ma) {
  Guard g;
  if (pendingCount >= LOG_BUFFER_RECORDS) flushLocked();
  if (pendingCount >= LOG_BUFFER_RECORDS) {       // flash en panne : on garde les plus récentes
    memmove(&pending[0], &pending[1], sizeof(Record) * (LOG_BUFFER_RECORDS - 1));
    --pendingCount;
  }
  pending[pendingCount++] = {epoch, up, boot, packed, mv, ma};
  if (pendingCount >= LOG_FLUSH_RECORDS) flushLocked();
}

void timeKnown(uint32_t epochNow, uint32_t upNow) {
  Guard g;
  for (uint16_t i = 0; i < pendingCount; ++i)
    if (pending[i].epoch == 0 && pending[i].boot == boot && pending[i].up <= upNow)
      pending[i].epoch = epochNow - (upNow - pending[i].up);
}

void flush() { Guard g; flushLocked(); }

uint32_t firstSeq() { Guard g; return firstFile * LOG_FILE_RECORDS; }
uint32_t nextSeq()  { Guard g; return flashEnd() + pendingCount; }

size_t read(uint32_t fromSeq, uint8_t* out, size_t max) {
  Guard g;
  const uint32_t first = firstFile * LOG_FILE_RECORDS;
  const uint32_t onFlash = flashEnd();
  const uint32_t next = onFlash + pendingCount;
  uint32_t from = fromSeq < first ? first : fromSeq;
  if (from > next) from = next;
  if (max < 16) return 0;
  const uint32_t room = (max - 16) / sizeof(Record);
  uint32_t count = min<uint32_t>(room, next - from);

  uint8_t* p = out + 16;
  uint32_t seq = from, got = 0;
  while (got < count) {
    if (seq < onFlash) {
      // Depuis la flash, sans dépasser la fin du fichier courant.
      const uint32_t file = seq / LOG_FILE_RECORDS, pos = seq % LOG_FILE_RECORDS;
      const uint32_t inFile = (file == lastFile ? lastCount : LOG_FILE_RECORDS) - pos;
      const uint32_t n = min<uint32_t>(count - got, inFile);
      File f = LittleFS.open(fileName(file), FILE_READ);
      size_t r = 0;
      if (f && f.seek(pos * sizeof(Record))) r = f.read(p, n * sizeof(Record));
      if (f) f.close();
      if (r != n * sizeof(Record)) break;          // fichier illisible : on s'arrête là
      p += r; seq += n; got += n;
    } else {
      const uint32_t i = seq - onFlash;
      const uint32_t n = min<uint32_t>(count - got, pendingCount - i);
      memcpy(p, &pending[i], n * sizeof(Record));
      p += n * sizeof(Record); seq += n; got += n;
    }
  }
  put32(out, first);
  put32(out + 4, next);
  put32(out + 8, from);
  put16(out + 12, (uint16_t)got);
  put16(out + 14, sizeof(Record));
  return 16 + got * sizeof(Record);
}

}  // namespace DataLog
