#include "lighting.h"
#include "config.h"

namespace {
  constexpr uint32_t PWM_MAX = (1u << LIGHTING_PWM_BITS) - 1;

  // Une voie PWM par sortie. Index 0 = intérieur, 1 = extérieur.
  constexpr uint8_t PINS[2] = { PIN_PWM_INTERIOR, PIN_PWM_EXTERIOR };

  uint8_t idx(dusk::Output out) { return out == dusk::Output::Interior ? 0 : 1; }

  void writeDuty(uint8_t i, uint32_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(PINS[i], duty);
#else
    ledcWrite(i, duty);          // ancienne API : on écrit sur le canal LEDC n° i
#endif
  }
}

namespace Lighting {

void begin() {
  // L'API PWM (LEDC) a changé entre les versions 2 et 3 du cœur Arduino ESP32.
  // Ce code compile avec les deux.
  for (uint8_t i = 0; i < 2; i++) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(PINS[i], LIGHTING_PWM_FREQ, LIGHTING_PWM_BITS);
#else
    ledcSetup(i, LIGHTING_PWM_FREQ, LIGHTING_PWM_BITS);
    ledcAttachPin(PINS[i], i);
#endif
    writeDuty(i, 0);   // éteint au démarrage
  }
}

void setLevel(dusk::Output out, float level) {
  if (level < 0.0f) level = 0.0f;
  if (level > 1.0f) level = 1.0f;
  // L'œil perçoit la luminosité de façon logarithmique : sans correction,
  // le fondu semble se faire presque entièrement au début. Gamma 2,2 = norme courante.
  float corrected = powf(level, 2.2f);
  writeDuty(idx(out), (uint32_t)(corrected * PWM_MAX + 0.5f));
}

void updateFadeTest() {
  // Position dans un cycle complet montée + descente, sans bloquer.
  uint32_t t = millis() % (2 * LIGHTING_FADE_MS);
  float level = (t < LIGHTING_FADE_MS)
    ? (float)t / LIGHTING_FADE_MS
    : 1.0f - (float)(t - LIGHTING_FADE_MS) / LIGHTING_FADE_MS;
  setLevel(dusk::Output::Interior, level);
  setLevel(dusk::Output::Exterior, level);
}

}  // namespace Lighting
