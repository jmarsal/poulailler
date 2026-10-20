// config.h
// Tous les réglages du montage, au même endroit.
// Les modules ne contiennent aucune valeur en dur : ils lisent ce fichier.

#pragma once
#include <Arduino.h>
#include "dusk_controller.h"

// ---------------------------------------------------------------------------
// Modules activés
// ---------------------------------------------------------------------------
// Banc d'essai du fondu PWM (étape 3) : remplace la détection du crépuscule par
// un fondu en boucle, sur les deux sorties à la fois. Sous USB, n'utiliser qu'un petit bout de ruban (quelques
// LED) : un ruban COB entier tire plus d'un ampère, trop pour le port USB.
constexpr bool ENABLE_LIGHTING_TEST = false;

// Réservés pour plus tard, pas encore écrits :
// ENABLE_SENSORS  (thermomètre 1-Wire ou I2C sur le bornier d'attente)
// ENABLE_NETWORK  (WiFi, MQTT, Home Assistant, mise à jour sans fil)

// ---------------------------------------------------------------------------
// Affectation des broches (voir README, section câblage)
// ---------------------------------------------------------------------------
constexpr uint8_t PIN_LIGHT_SIGNAL = 0;   // ADC : broche S du KY-018
constexpr uint8_t PIN_LIGHT_POWER  = 1;   // alimente le KY-018 pendant la mesure seulement
constexpr uint8_t PIN_ONEWIRE      = 5;   // réservé : sondes DS18B20
constexpr uint8_t PIN_I2C_SDA      = 6;   // réservé : bus I2C, données
constexpr uint8_t PIN_I2C_SCL      = 7;   // réservé : bus I2C, horloge
constexpr uint8_t PIN_STATUS_LED   = 8;   // LED bleue de la carte, active à l'état BAS
constexpr uint8_t PIN_PWM_INTERIOR = 10;  // PWM du D4184 du ruban intérieur (sablière)
constexpr uint8_t PIN_PWM_EXTERIOR = 4;   // PWM du D4184 du ruban extérieur (entrée)
// GPIO9 = bouton BOOT, GPIO20/21 = port série, GPIO2 = broche de démarrage : à éviter.
// Libre : GPIO3 (ADC), réservé à la mesure de la batterie.

// ---------------------------------------------------------------------------
// Capteur de lumière
// ---------------------------------------------------------------------------
// Câblage retenu (16 octobre) : GPIO1 sur la patte − du KY-018, masse sur la
// patte du milieu. Plus de lumière = valeur plus haute.
// Repères du banc d'essai : doigt posé ~285, pièce ~4000, lampe 4095 (saturé).
//
// Temps laissé au pont diviseur pour se stabiliser après mise sous tension.
constexpr uint16_t LIGHT_SETTLE_MS     = 10;
// Nombre de lectures moyennées par mesure (atténue le bruit de l'ADC).
constexpr uint8_t  LIGHT_SAMPLES       = 16;

// ---------------------------------------------------------------------------
// Détection du crépuscule
// ---------------------------------------------------------------------------
// true  : temps accéléré, pour les essais sur le bureau (soirée complète en 30 s).
// false : durées réelles, pour le poulailler. Réglé sur false le 19 octobre,
//         après validation du montage complet sur accus et panneau.
constexpr bool FAST_TEST = false;

// Seuils en valeur brute lissée (0 à 4095). Ceux du bureau sont choisis pour
// se déclencher en cachant le capteur avec la main. Ceux du poulailler sont
// provisoires : ils se régleront sur place, au crépuscule, juste au-dessus du
// moment où la porte Kerbl se ferme.
//
// Profils (décidés le 16 octobre), comptés depuis le crépuscule :
//   extérieur : 30 min allumé, puis extinction (la porte Kerbl se ferme) ;
//   intérieur : 30 min de pleine lumière, puis 30 min de baisse progressive.
// Profil = { montée, fin de la pleine lumière, durée de la baisse, intensité max }.
// L'intensité max (0.0 à 1.0) sert à brider un ruban trop fort ou trop gourmand.
constexpr uint32_t MIN = 60UL * 1000;

constexpr dusk::Config DUSK_FAST = {
  /* duskThreshold */ 1000,
  /* dayThreshold  */ 2500,
  /* smoothingMs   */ 2000,          // 2 s
  /* duskConfirmMs */ 5000,          // 5 s dans le noir avant d'allumer
  /* dayConfirmMs  */ 5000,          // 5 s de lumière avant de réarmer
  /* minNightMs    */ 50000,         // verrou de 50 s après le déclenchement
  // Intensité bridée à 0.5 sur le banc d'essai : avec la correction gamma, environ
  // un quart du courant maximal, pour ménager le chargeur USB qui alimente le ruban.
  /* interior      */ { 2000, 15000, 15000, 0.5f },   // 15 s plein, 15 s de baisse
  /* exterior      */ { 2000, 15000,  1000, 0.5f },   // 15 s, puis extinction en 1 s
};

constexpr dusk::Config DUSK_REAL = {
  /* duskThreshold */ 1000,          // à régler au poulailler
  /* dayThreshold  */ 2500,          // à régler au poulailler
  /* smoothingMs   */ 5 * MIN,
  /* duskConfirmMs */ 10 * MIN,      // 10 min sous le seuil
  /* dayConfirmMs  */ 15 * MIN,      // 15 min au-dessus du seuil de jour
  /* minNightMs    */ 8 * 60 * MIN,  // pas de réarmement avant 8 h
  // Intensité max de l'intérieur : 1.0 pour 60 cm de ruban (environ 0,32 A).
  // Avec 1 m de ruban, passer à 0.6f : lumière plus étalée, environ 0,16 A.
  /* interior      */ { 5000, 30 * MIN, 30 * MIN, 1.0f },
  /* exterior      */ { 5000, 35 * MIN, 10000,    1.0f },  // 15 min avant la fermeture de la porte + 20 min après ; extinction en 10 s
};

constexpr dusk::Config DUSK_CONFIG     = FAST_TEST ? DUSK_FAST : DUSK_REAL;
// Intervalle entre deux mesures de lumière.
constexpr uint32_t     LIGHT_PERIOD_MS = FAST_TEST ? 500 : 30000;

// ---------------------------------------------------------------------------
// LED de la carte
// ---------------------------------------------------------------------------
constexpr uint16_t STATUS_BLINK_ON_MS  = 50;   // éclair bref à chaque mesure

// ---------------------------------------------------------------------------
// Éclairage (banc d'essai du fondu)
// ---------------------------------------------------------------------------
constexpr uint32_t LIGHTING_PWM_FREQ   = 20000; // 20 kHz : inaudible, pas de scintillement
constexpr uint8_t  LIGHTING_PWM_BITS   = 10;    // 0 à 1023
constexpr uint32_t LIGHTING_FADE_MS    = 5000;  // durée d'une montée ou d'une descente

// ---------------------------------------------------------------------------
// Série
// ---------------------------------------------------------------------------
constexpr uint32_t SERIAL_BAUD         = 115200;
// Laisse au Mac le temps de reconnaître le port USB avant les premiers messages.
constexpr uint16_t SERIAL_WAIT_MS      = 2000;

// ---------------------------------------------------------------------------
// Bluetooth (commande manuelle depuis le téléphone)
// ---------------------------------------------------------------------------
// Bluetooth basse consommation (BLE) : depuis l'iPhone, appli nRF Connect.
// Commandes texte : « ext on », « ext off », « int on », « int off », « off », « etat ».
// Connexion protégée par un code à 6 chiffres, dans include/secrets.h (hors Git).
// Coût : quelques mA de plus en permanence, et incompatible avec une future
// veille profonde. Mettre false pour le couper.
constexpr bool        ENABLE_BLE      = true;
constexpr const char* BLE_NAME        = "Poulailler";
// Appui de 3 s sur le bouton BOOT, carte en marche : efface les appairages Bluetooth.
// (Ne pas le tenir pendant la mise sous tension : la carte démarrerait en mode programmation.)
constexpr uint8_t     BOOT_BUTTON_PIN = 9;
constexpr uint32_t    BOOT_HOLD_MS    = 3000;
// Sûreté : un allumage manuel s'éteint seul au bout de ce délai, quoi qu'il arrive.
// L'éclairage de l'entrée retient la porte Kerbl ouverte : jamais d'allumage sans fin.
constexpr uint32_t    MANUAL_MAX_MS   = 15 * MIN;

// ---------------------------------------------------------------------------
// Historique (courbe de lumière et journal des soirées, en mémoire vive)
// ---------------------------------------------------------------------------
// 240 points espacés de 6 min = 24 h de courbe. Perdu en cas de redémarrage.
constexpr uint16_t HISTORY_POINTS    = 240;
constexpr uint32_t HISTORY_PERIOD_MS = FAST_TEST ? 5000 : 6 * MIN;
constexpr uint8_t  JOURNAL_EVENINGS  = 7;    // dernières soirées gardées

// Journal longue durée en flash (datalog) : une mesure par HISTORY_PERIOD_MS.
// 12 fichiers de 2048 mesures (32 ko chacun) : environ 100 jours à 6 min.
constexpr uint16_t LOG_FILE_RECORDS   = 2048;
constexpr uint8_t  LOG_FILES          = 12;
// Écriture en flash à chaque mesure : une coupure (accu à plat) ne perd rien.
// Usure négligeable : LittleFS répartit les écritures sur 1,4 Mo, soit environ
// un effacement de bloc par jour pour des blocs donnés pour 100 000 cycles.
constexpr uint16_t LOG_FLUSH_RECORDS  = 1;
constexpr uint16_t LOG_BUFFER_RECORDS = 32;   // attente maximale en mémoire vive
