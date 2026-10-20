// ble_control.h
// Commande par Bluetooth basse consommation (BLE).
// Le téléphone se connecte à l'appareil « Poulailler » (code à 6 chiffres) et voit
// un service avec quatre caractéristiques (des cases de données) :
//   - « commande » : on y écrit un texte, par exemple « ext on » ou « set dusk 1200 » ;
//   - « etat »     : état courant en JSON, envoyé au téléphone à chaque mise à jour ;
//   - « infos »    : réglages, santé de la carte et journal des soirées, en JSON ;
//   - « courbe »   : les dernières 24 h de lumière, en binaire ;
//   - « ota »      : les morceaux d'un nouveau programme, pendant une mise à jour.
// Ce module ne décide de rien : il transmet les commandes à main.cpp.

#pragma once
#include <Arduino.h>

namespace BleControl {
  void begin();
  // Rend true et remplit `out` si une commande attend (file de quelques commandes).
  bool pollCommand(String& out);
  // Met à jour l'état (et prévient le téléphone s'il est abonné).
  void publishState(const String& json);
  void setInfo(const String& json);
  void setCurve(const uint8_t* data, size_t len);
  bool connected();
  // Efface tous les appairages enregistrés (appui long sur BOOT) : le téléphone
  // devra « Oublier cet appareil » puis redonner le code.
  void clearBonds();

  // Mise à jour du programme par Bluetooth (OTA, « over the air »).
  // Le téléphone envoie « ota begin <taille> », puis le fichier firmware.bin par
  // morceaux sur la caractéristique « ota », puis « ota end ». La carte vérifie
  // le programme reçu avant de l'adopter ; sinon elle garde l'ancien.
  String otaBegin(size_t size);
  bool   otaEnd(String& msg);       // true : nouveau programme prêt, redémarrer
  void   otaAbort();
  bool   otaActive();
  size_t otaReceived();
}
