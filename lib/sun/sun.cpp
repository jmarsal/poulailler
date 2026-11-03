#include "sun.h"
#include <cmath>

namespace sun {

namespace {
  constexpr double PI = 3.14159265358979323846;
  constexpr double RAD = PI / 180.0;
  constexpr double J1970 = 2440587.5;   // jour julien du 1er janvier 1970, 0 h UTC
  constexpr double J2000 = 2451545.0;   // jour julien du 1er janvier 2000, 12 h UTC

  double norm360(double a) {
    a = std::fmod(a, 360.0);
    return a < 0 ? a + 360.0 : a;
  }
}

uint32_t sunsetUtc(uint32_t epoch, double latitude, double longitude) {
  // Jour julien de midi UTC, pour le jour qui contient `epoch`.
  const double day = std::floor(epoch / 86400.0);
  const double noon = J1970 + day + 0.5;

  // Jour solaire moyen, corrigé de la longitude (positive vers l'est).
  const double n = std::round(noon - J2000 + 0.0008);
  const double jStar = n - longitude / 360.0;

  // Anomalie moyenne, équation du centre, longitude écliptique.
  const double m = norm360(357.5291 + 0.98560028 * jStar);
  const double c = 1.9148 * std::sin(m * RAD) + 0.0200 * std::sin(2 * m * RAD)
                 + 0.0003 * std::sin(3 * m * RAD);
  const double lambda = norm360(m + c + 180.0 + 102.9372);

  // Passage du soleil au méridien (midi solaire), en jour julien.
  const double transit = J2000 + jStar + 0.0053 * std::sin(m * RAD)
                       - 0.0069 * std::sin(2 * lambda * RAD);

  // Déclinaison, puis angle horaire du coucher.
  const double sinDecl = std::sin(lambda * RAD) * std::sin(23.4397 * RAD);
  const double cosDecl = std::cos(std::asin(sinDecl));
  const double cosOmega = (std::sin(-0.833 * RAD) - std::sin(latitude * RAD) * sinDecl)
                        / (std::cos(latitude * RAD) * cosDecl);
  if (cosOmega < -1.0 || cosOmega > 1.0) return 0;   // jour ou nuit polaire
  const double omega = std::acos(cosOmega) / RAD;

  const double set = transit + omega / 360.0;
  return (uint32_t)std::llround((set - J1970) * 86400.0);
}

}  // namespace sun
