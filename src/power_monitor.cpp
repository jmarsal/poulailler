// power_monitor.cpp
// Pilote minimal de l'INA219, sans bibliothèque : trois registres suffisent.
#include "power_monitor.h"
#include "config.h"
#include <Wire.h>

namespace {
  constexpr uint8_t ADDR = 0x40;
  constexpr uint8_t REG_CONFIG = 0x00, REG_SHUNT = 0x01, REG_BUS = 0x02;
  // Bus 16 V, shunt ±320 mV, conversions 12 bits, mode « shunt et bus en continu ».
  constexpr uint16_t CFG_RUN  = 0x199F;
  constexpr uint16_t CFG_DOWN = 0x1998;   // même réglage, mode veille (bits de mode à 000)

  bool found = false;
  PowerMonitor::Reading lastReading = {false, 0, 0};

  bool write16(uint8_t reg, uint16_t v) {
    Wire.beginTransmission(ADDR);
    Wire.write(reg); Wire.write(v >> 8); Wire.write(v & 0xFF);
    return Wire.endTransmission() == 0;
  }
  bool read16(uint8_t reg, uint16_t& v) {
    Wire.beginTransmission(ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)ADDR, 2) != 2) return false;
    v = (uint16_t)Wire.read() << 8;
    v |= Wire.read();
    return true;
  }
}

namespace PowerMonitor {

bool begin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.beginTransmission(ADDR);
  found = Wire.endTransmission() == 0 && write16(REG_CONFIG, CFG_DOWN);
  return found;
}

bool present() { return found; }

Reading read() {
  Reading r = {false, 0, 0};
  if (!found) return r;
  uint16_t shunt = 0, bus = 0;
  // Réveil, deux conversions de 12 bits (environ 1,1 ms), lecture, veille.
  if (write16(REG_CONFIG, CFG_RUN)) {
    delay(2);
    if (read16(REG_SHUNT, shunt) && read16(REG_BUS, bus)) {
      // Shunt : 10 µV par unité, 0,1 Ω → 0,1 mA par unité. Signe inversé :
      // le courant qui sort de l'accu (décharge) traverse le shunt de VIN+ vers VIN−.
      const int32_t ma = -(int32_t)(int16_t)shunt / 10;
      r.ma = (int16_t)constrain(ma, -32767, 32767);
      r.mv = (uint16_t)((bus >> 3) * 4);   // bus : 4 mV par unité, à partir du bit 3
      r.ok = r.mv > 1000;                  // moins de 1 V : module mal branché
    }
    write16(REG_CONFIG, CFG_DOWN);
  }
  if (r.ok) lastReading = r;
  return r;
}

Reading last() { return lastReading; }

}  // namespace PowerMonitor
