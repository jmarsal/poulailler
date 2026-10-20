// datalog.h
// Journal de mesures longue durée, gardé dans la mémoire flash de la carte (LittleFS).
//
// Une mesure toutes les HISTORY_PERIOD_MS (6 min) : 16 octets avec l'heure réelle
// (si le téléphone l'a donnée), le temps depuis le démarrage, la lumière lissée,
// l'état des rubans, et deux cases réservées à la tension et au courant de l'accu
// (futur capteur INA219).
//
// Chaque mesure a un numéro qui ne fait que croître. Le téléphone demande « tout
// ce qui suit le numéro N » et garde sa copie. La carte n'efface rien à la
// synchronisation : elle garde environ LOG_FILES × LOG_FILE_RECORDS mesures en
// tournant (les plus anciennes sont écrasées), soit une centaine de jours.
//
// Chaque mesure est écrite en flash dès qu'elle est prise (LOG_FLUSH_RECORDS) :
// une coupure de courant ne perd rien.

#pragma once
#include <Arduino.h>

namespace DataLog {
  constexpr int16_t NO_CURRENT = INT16_MIN;   // courant absent (pas de capteur)

  struct __attribute__((packed)) Record {
    uint32_t epoch;    // heure réelle en secondes (0 si inconnue au moment de la mesure)
    uint32_t up;       // secondes depuis le démarrage
    uint16_t boot;     // numéro de démarrage (16 bits faibles)
    uint16_t packed;   // 12 bits lumière, bit 12 intérieur allumé, bit 13 entrée allumée
    uint16_t mv;       // tension de l'accu en mV (0 si absente)
    int16_t  ma;       // courant de l'accu en mA, positif en charge (NO_CURRENT si absent)
  };
  static_assert(sizeof(Record) == 16, "une mesure = 16 octets");

  // Monte le système de fichiers (le formate au tout premier démarrage).
  bool begin(uint16_t bootId);
  bool ok();

  void add(uint32_t epoch, uint32_t up, uint16_t packed, uint16_t mv, int16_t ma);
  // L'heure réelle vient d'arriver : complète les mesures encore en mémoire vive.
  void timeKnown(uint32_t epochNow, uint32_t upNow);
  // Écrit en flash les mesures en attente (avant un redémarrage par exemple).
  void flush();

  uint32_t firstSeq();   // plus ancienne mesure encore disponible
  uint32_t nextSeq();    // numéro de la prochaine mesure

  // Réponse au téléphone : en-tête de 16 octets
  //   uint32 premier disponible, uint32 suivant, uint32 début de ce paquet,
  //   uint16 nombre de mesures, uint16 taille d'une mesure (16)
  // puis les mesures. Renvoie le nombre d'octets écrits.
  size_t read(uint32_t fromSeq, uint8_t* out, size_t max);
}
