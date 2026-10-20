// settings.cpp
#include "settings.h"
#include "config.h"
#include <Preferences.h>

namespace {
  constexpr const char* NS = "reglages";
  constexpr uint16_t MIN_GAP = 200;   // écart minimal entre seuil de jour et de crépuscule

  uint16_t toMin(uint32_t ms) { return (uint16_t)((ms + MIN / 2) / MIN); }
  uint8_t toPct(float f) { return (uint8_t)(f * 100 + 0.5f); }

  // Lit un entier après le nom de réglage ; false si absent ou hors bornes.
  bool parseInt(const String& s, long lo, long hi, long& out) {
    if (s.length() == 0) return false;
    for (size_t i = 0; i < s.length(); ++i) if (!isDigit(s[i])) return false;
    out = s.toInt();
    return out >= lo && out <= hi;
  }
}

namespace Settings {

dusk::Config load() {
  dusk::Config cfg = DUSK_CONFIG;
  if (FAST_TEST) return cfg;
  Preferences p;
  p.begin(NS, true);
  cfg.duskThreshold     = p.getUShort("dusk", cfg.duskThreshold);
  cfg.dayThreshold      = p.getUShort("day", cfg.dayThreshold);
  cfg.interior.holdMs   = p.getUInt("ihold", cfg.interior.holdMs);
  cfg.interior.fadeOutMs= p.getUInt("ifade", cfg.interior.fadeOutMs);
  cfg.interior.maxLevel = p.getFloat("imax", cfg.interior.maxLevel);
  cfg.exterior.holdMs   = p.getUInt("ehold", cfg.exterior.holdMs);
  cfg.exterior.maxLevel = p.getFloat("emax", cfg.exterior.maxLevel);
  p.end();
  return cfg;
}

String apply(const String& cmdIn, dusk::Config& cfg) {
  if (FAST_TEST) return "réglages indisponibles en mode test";

  String cmd = cmdIn;
  if (cmd == "reset") {
    Preferences p;
    p.begin(NS, false);
    p.clear();
    p.end();
    cfg = DUSK_CONFIG;
    return "réglages d'origine rétablis";
  }

  // « set nom valeur »
  int sp1 = cmd.indexOf(' ');
  int sp2 = cmd.indexOf(' ', sp1 + 1);
  if (sp1 < 0 || sp2 < 0) return "commande incomplète";
  String name = cmd.substring(sp1 + 1, sp2);
  String val = cmd.substring(sp2 + 1);
  long v;

  Preferences p;
  p.begin(NS, false);
  String msg;

  if (name == "dusk") {
    if (!parseInt(val, 20, 4000, v)) msg = "seuil crépuscule : de 20 à 4000";
    else if (v + MIN_GAP > cfg.dayThreshold) msg = "seuil crépuscule trop proche du seuil jour";
    else { cfg.duskThreshold = v; p.putUShort("dusk", v); msg = "seuil crépuscule : " + String(v); }
  } else if (name == "day") {
    if (!parseInt(val, 220, 4095, v)) msg = "seuil jour : de 220 à 4095";
    else if (v < cfg.duskThreshold + MIN_GAP) msg = "seuil jour trop proche du seuil crépuscule";
    else { cfg.dayThreshold = v; p.putUShort("day", v); msg = "seuil jour : " + String(v); }
  } else if (name == "int_hold") {
    if (!parseInt(val, 1, 240, v)) msg = "intérieur plein : de 1 à 240 min";
    else { cfg.interior.holdMs = v * MIN; p.putUInt("ihold", cfg.interior.holdMs); msg = "intérieur plein : " + String(v) + " min"; }
  } else if (name == "int_fade") {
    if (!parseInt(val, 0, 120, v)) msg = "intérieur baisse : de 0 à 120 min";
    else { cfg.interior.fadeOutMs = v * MIN; p.putUInt("ifade", cfg.interior.fadeOutMs); msg = "intérieur baisse : " + String(v) + " min"; }
  } else if (name == "int_max") {
    if (!parseInt(val, 10, 100, v)) msg = "intérieur intensité : de 10 à 100 %";
    else { cfg.interior.maxLevel = v / 100.0f; p.putFloat("imax", cfg.interior.maxLevel); msg = "intérieur intensité : " + String(v) + " %"; }
  } else if (name == "ext_hold") {
    if (!parseInt(val, 1, 120, v)) msg = "entrée : de 1 à 120 min";
    else { cfg.exterior.holdMs = v * MIN; p.putUInt("ehold", cfg.exterior.holdMs); msg = "entrée : " + String(v) + " min"; }
  } else if (name == "ext_max") {
    if (!parseInt(val, 10, 100, v)) msg = "entrée intensité : de 10 à 100 %";
    else { cfg.exterior.maxLevel = v / 100.0f; p.putFloat("emax", cfg.exterior.maxLevel); msg = "entrée intensité : " + String(v) + " %"; }
  } else {
    msg = "réglage inconnu : " + name;
  }
  p.end();
  return msg;
}

String toJson(const dusk::Config& cfg) {
  char buf[200];
  snprintf(buf, sizeof(buf),
           "{\"dusk\":%u,\"day\":%u,\"ih\":%u,\"if\":%u,\"im\":%u,\"eh\":%u,\"em\":%u,\"man\":%u}",
           cfg.duskThreshold, cfg.dayThreshold,
           toMin(cfg.interior.holdMs), toMin(cfg.interior.fadeOutMs), toPct(cfg.interior.maxLevel),
           toMin(cfg.exterior.holdMs), toPct(cfg.exterior.maxLevel),
           (unsigned)(MANUAL_MAX_MS / MIN));
  return String(buf);
}

}  // namespace Settings
