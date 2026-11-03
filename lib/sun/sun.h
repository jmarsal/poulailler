// sun.h
// Heure du coucher du soleil, calculée sur place à partir de la date et des
// coordonnées du poulailler. Aucun réseau, aucune table à mettre à jour.
//
// Méthode : équation du lever et du coucher du soleil (algorithme simplifié de
// la NOAA, aussi décrit sur Wikipédia, « Sunrise equation »). Le coucher est
// l'instant où le haut du disque solaire passe sous l'horizon, réfraction
// comprise (hauteur de -0,833°). Précision : une à deux minutes, largement
// assez pour une fenêtre de plusieurs dizaines de minutes.
//
// Tout se fait en temps universel (secondes depuis 1970, UTC) : pas besoin de
// fuseau horaire ni de changement d'heure. En France, le coucher du soleil
// tombe toujours le même jour en UTC qu'en heure locale.
//
// Logique pure, testée sur le Mac (pio test -e native).

#pragma once
#include <cstdint>

namespace sun {

// Coucher du soleil (secondes UTC depuis 1970) du jour UTC qui contient
// `epoch`. latitude : degrés, positive au nord ; longitude : degrés, positive
// à l'est. Renvoie 0 si le soleil ne se couche pas ce jour-là (régions polaires).
uint32_t sunsetUtc(uint32_t epoch, double latitude, double longitude);

}  // namespace sun
