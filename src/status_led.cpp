#include "status_led.h"
#include "config.h"

namespace {
  uint32_t offAt = 0;   // instant où éteindre la LED, 0 si rien de prévu
}

namespace StatusLed {

void begin() {
  pinMode(PIN_STATUS_LED, OUTPUT);
  off();
}

void on()  { digitalWrite(PIN_STATUS_LED, LOW);  }   // active à l'état bas
void off() { digitalWrite(PIN_STATUS_LED, HIGH); }

void flash(uint16_t durationMs) {
  on();
  offAt = millis() + durationMs;
}

void update() {
  // Comparaison par différence : reste juste quand millis() repasse par zéro (tous les 49 jours).
  if (offAt != 0 && (int32_t)(millis() - offAt) >= 0) {
    off();
    offAt = 0;
  }
}

}  // namespace StatusLed
