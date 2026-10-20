# Éclairage du poulailler

ESP32-C3 Super Mini, PlatformIO, framework Arduino.
Détection du crépuscule, puis deux éclairages : l'extérieur (entrée) 30 min, l'intérieur 30 min de pleine lumière suivies de 30 min de baisse progressive.

Pour le câblage pas à pas avec schémas, voir la page « Notice du banc d'essai ».

## Organisation du code

| Fichier | Rôle |
|---|---|
| `include/config.h` | Tous les réglages : broches, seuils, durées, mode temps accéléré |
| `lib/dusk_controller/` | Logique du crépuscule, sans matériel, testée sur le Mac |
| `src/light_sensor.*` | KY-018, alimenté seulement pendant la mesure, moyenne de 16 lectures |
| `src/lighting.*` | PWM vers les deux modules MOSFET D4184 (intérieur, extérieur), correction gamma |
| `src/status_led.*` | LED bleue de la carte, témoin |
| `src/main.cpp` | Relie capteur, logique, sortie PWM et LED |
| `test/test_dusk/` | Tests de la logique du crépuscule |

## Commandes

```
pio run -t upload        # compiler et envoyer sur la carte
pio device monitor       # moniteur série (Ctrl+C pour quitter)
pio test -e native       # tests de la logique, sur le Mac, sans la carte
```

## Détection du crépuscule

Phases : attente du jour → jour → soirée → nuit → (le matin) jour.

Pendant la soirée, chaque sortie suit son profil (dans `config.h`) : montée,
pleine lumière, baisse, extinction. La soirée se termine quand la dernière sortie s'éteint.

- Au démarrage, rien ne s'allume tant que le jour n'a pas été vu : une carte qui
  redémarre en pleine nuit reste éteinte.
- Le crépuscule doit durer (`duskConfirmMs`) pour déclencher : une ombre qui passe ne fait rien.
- Un seul cycle par soir : après l'allumage, le réarmement est verrouillé pendant
  `minNightMs` (8 h en réel). La lumière de nos rubans sur le capteur ne compte pas.
- Hystérésis : seuil de jour nettement au-dessus du seuil de crépuscule.

`FAST_TEST = true` dans `config.h` accélère tout pour les essais sur le bureau :
5 s de noir pour allumer ; extérieur 15 s ; intérieur 15 s plein puis 15 s de baisse ;
verrou de 50 s ; 5 s de lumière pour réarmer.

Limite connue : sans horloge, un orage très sombre en fin d'après-midi pourrait
déclencher l'éclairage en avance. Les 10 minutes de confirmation et le lissage
de 5 minutes réduisent ce risque sans l'annuler.

## Brochage de la carte (vue de dessus, USB-C à gauche)

```
              5V   G  3V3  4   3   2   1   0
              ●    ●   ●   ●   ●   ●   ●   ●
        ┌──┐┌───────────────────────────────┐
  USB-C │  ││          ESP32-C3              │
        └──┘└───────────────────────────────┘
              ●    ●   ●   ●   ●   ●   ●   ●
              5    6   7   8   9   10  20  21
```

## Câblage du capteur KY-018

Câblage volontairement inversé par rapport aux inscriptions du module, pour que
plus de lumière donne une valeur plus haute et que toute la plage de mesure serve
au crépuscule.

| Fil | ESP32-C3 | KY-018 |
|---|---|---|
| jaune | GPIO0 | S |
| rouge | GPIO1 | − |
| noir | G | patte du milieu |

Repères (16 octobre) : doigt posé ~285, pièce ~4000, lampe du téléphone 4095 (saturé).
Plafond de l'ADC sur cette carte : 2884 mV.

## Broches

| Broche | Usage |
|---|---|
| GPIO0 | signal du capteur de lumière (ADC) |
| GPIO1 | alimentation du capteur de lumière |
| GPIO5 | réservé, bus 1-Wire |
| GPIO6, GPIO7 | réservés, bus I2C (SDA, SCL) |
| GPIO8 | LED de la carte |
| GPIO9 | bouton BOOT |
| GPIO10 | PWM vers le D4184 du ruban intérieur |
| GPIO4 | PWM vers le D4184 du ruban extérieur |
| GPIO20, GPIO21 | port série, à garder libres |
| GPIO2 | à éviter (démarrage) |
| GPIO3 | libre, réservé à la mesure de la batterie |

## Application téléphone

La page de commande Bluetooth est dans `docs/` et publiée par GitHub Pages : https://jmarsal.github.io/poulailler/ (à ouvrir dans Bluefy sur iPhone).
