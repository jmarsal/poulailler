#include "light_sensor.h"
#include "config.h"

namespace LightSensor {

void begin() {
  pinMode(PIN_LIGHT_POWER, OUTPUT);
  digitalWrite(PIN_LIGHT_POWER, LOW);   // capteur éteint par défaut

  // Résolution 12 bits : 0 à 4095.
  analogReadResolution(12);
  // Atténuation 11 dB : la plus large plage de mesure. Sur l'ESP32-C3, elle monte
  // à environ 2,5 V seulement. Au-delà, la lecture plafonne à 4095 (voir README).
  analogSetPinAttenuation(PIN_LIGHT_SIGNAL, ADC_11db);
}

Reading read() {
  digitalWrite(PIN_LIGHT_POWER, HIGH);
  delay(LIGHT_SETTLE_MS);

  uint32_t sumRaw = 0;
  uint32_t sumMv  = 0;
  for (uint8_t i = 0; i < LIGHT_SAMPLES; i++) {
    sumRaw += analogRead(PIN_LIGHT_SIGNAL);
    sumMv  += analogReadMilliVolts(PIN_LIGHT_SIGNAL);
  }

  digitalWrite(PIN_LIGHT_POWER, LOW);

  Reading r;
  r.raw        = sumRaw / LIGHT_SAMPLES;
  r.millivolts = sumMv  / LIGHT_SAMPLES;
  return r;
}

}  // namespace LightSensor
