// main.cpp
// Détection du crépuscule et éclairage du soir.
// La logique est dans lib/dusk_controller (testée sur le Mac) ; ce fichier
// relie le capteur, la logique, la sortie PWM, la LED de la carte et le Bluetooth.
//
// Deux sorties : ruban intérieur (GPIO10) et ruban extérieur (GPIO4).
// Pendant la soirée, la LED bleue de la carte reste allumée : on voit le cycle
// même sans ruban branché, et le moniteur affiche le niveau de chaque sortie.
//
// Bluetooth (ble_control) : allumage manuel (« ext on », MANUAL_MAX_MS au plus),
// réglages enregistrés (settings), courbe et journal (history), heure réelle
// donnée par le téléphone, santé de la carte.

#include <Arduino.h>
#include <Preferences.h>
#include <sys/time.h>
#include <esp_system.h>
#include "config.h"
#include "status_led.h"
#include "light_sensor.h"
#include "lighting.h"
#include "dusk_controller.h"
#include "ble_control.h"
#include "settings.h"
#include "history.h"

namespace {
  constexpr const char* FIRMWARE = "2026-10-20 c";

  dusk::Controller controller(DUSK_CONFIG);
  dusk::Config cfg = DUSK_CONFIG;
  uint32_t lastMeasure = 0;
  uint16_t lastRaw = 0;

  // Allumage manuel, une entrée par sortie (0 = intérieur, 1 = extérieur).
  bool     manualOn[2]    = {false, false};
  uint32_t manualStart[2] = {0, 0};

  // Santé et heure
  uint32_t boots = 0;
  const char* resetReason = "?";
  bool timeSet = false;
  String lastMsg = "";
  uint16_t msgCount = 0;

  int idx(dusk::Output o) { return o == dusk::Output::Interior ? 0 : 1; }
  int pct(float v) { return (int)(v * 100 + 0.5f); }

  bool manualActive(dusk::Output o, uint32_t now) {
    const int i = idx(o);
    if (manualOn[i] && now - manualStart[i] >= MANUAL_MAX_MS) {
      manualOn[i] = false;                       // délai de sûreté écoulé
      Serial.printf("---- manuel %s : fin du délai\n", i == 0 ? "int." : "ext.");
    }
    return manualOn[i];
  }

  int manualMinutesLeft(int i, uint32_t now) {
    return manualOn[i] ? (int)((MANUAL_MAX_MS - (now - manualStart[i])) / 60000 + 1) : 0;
  }

  const char* reasonName(esp_reset_reason_t r) {
    switch (r) {
      case ESP_RST_POWERON:  return "mise sous tension";
      case ESP_RST_SW:       return "redémarrage logiciel";
      case ESP_RST_PANIC:    return "plantage";
      case ESP_RST_INT_WDT:
      case ESP_RST_TASK_WDT:
      case ESP_RST_WDT:      return "chien de garde";
      case ESP_RST_BROWNOUT: return "tension trop basse";
      default:               return "autre";
    }
  }

  String clockText() {
    if (!timeSet) return "";
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    char buf[8];
    strftime(buf, sizeof(buf), "%H:%M", &lt);
    return String(buf);
  }

  void say(const String& msg) {
    lastMsg = msg;
    ++msgCount;
    Serial.printf("     %s\n", msg.c_str());
  }

  void printTransition(uint32_t now) {
    String clk = clockText();
    Serial.printf("---- %s (à %lu s%s%s)\n", dusk::phaseName(controller.phase()),
                  (unsigned long)(now / 1000), clk.length() ? ", " : "", clk.c_str());
    if (controller.phase() == dusk::Phase::Evening) History::eveningStart(controller.smoothed());
    if (controller.phase() == dusk::Phase::Night) History::eveningEnd();
  }

  // État courant, compact : il doit tenir dans une notification Bluetooth.
  String stateJson(uint32_t now, float in, float out) {
    char buf[200];
    String m = lastMsg;
    m.replace("\"", "'");
    snprintf(buf, sizeof(buf),
             "{\"p\":%d,\"r\":%u,\"s\":%.0f,\"i\":%d,\"e\":%d,\"mi\":%d,\"me\":%d,\"u\":%lu,\"c\":\"%s\",\"n\":%u,\"o\":%u,\"m\":\"%s\"}",
             (int)controller.phase(), lastRaw, controller.smoothed(), pct(in), pct(out),
             manualMinutesLeft(0, now), manualMinutesLeft(1, now),
             (unsigned long)History::uptimeSec(), clockText().c_str(), msgCount,
             (unsigned)BleControl::otaReceived(), m.c_str());
    return String(buf);
  }

  // Réglages, santé et journal : lus à la demande par le téléphone.
  String infoJson() {
    char head[220];
    snprintf(head, sizeof(head),
             "{\"ver\":\"%s\",\"mode\":\"%s\",\"boots\":%lu,\"rst\":\"%s\",\"ev\":%u,\"t\":%.1f,\"heap\":%u,\"u\":%lu,",
             FIRMWARE, FAST_TEST ? "test" : "reel", (unsigned long)boots, resetReason,
             History::eveningsCount(), temperatureRead(), (unsigned)(ESP.getFreeHeap() / 1024),
             (unsigned long)History::uptimeSec());
    return String(head) + "\"cfg\":" + Settings::toJson(cfg) + ",\"j\":" + History::journalJson() + "}";
  }

  void refreshCurve() {
    static uint8_t buf[8 + 2 * HISTORY_POINTS];
    size_t n = History::curveBytes(buf, sizeof(buf));
    if (n) BleControl::setCurve(buf, n);
  }

  // Commandes texte reçues par Bluetooth.
  void handleCommand(String cmd, uint32_t now) {
    cmd.trim();
    cmd.toLowerCase();
    Serial.printf("---- Bluetooth : « %s »\n", cmd.c_str());
    auto set = [&](int i, bool on) { manualOn[i] = on; manualStart[i] = now; };

    if      (cmd == "ext on")  { set(1, true);  say("entrée allumée pour " + String(MANUAL_MAX_MS / MIN) + " min"); }
    else if (cmd == "ext off") { set(1, false); say("entrée éteinte"); }
    else if (cmd == "int on")  { set(0, true);  say("intérieur allumé pour " + String(MANUAL_MAX_MS / MIN) + " min"); }
    else if (cmd == "int off") { set(0, false); say("intérieur éteint"); }
    else if (cmd == "off")     { set(0, false); set(1, false); say("tout éteint"); }
    else if (cmd == "etat")    { }
    else if (cmd.startsWith("time ")) {
      // Heure donnée par le téléphone, en secondes depuis 1970 (UTC).
      long epoch = cmd.substring(5).toInt();
      if (epoch > 1700000000L) {
        struct timeval tv = {epoch, 0};
        settimeofday(&tv, nullptr);
        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);   // heure de Paris, été comme hiver
        tzset();
        timeSet = true;
        say("heure réglée : " + clockText());
      }
    } else if (cmd.startsWith("ota begin ")) {
      say(BleControl::otaBegin((size_t)cmd.substring(10).toInt()));
    } else if (cmd == "ota end") {
      String msg;
      const bool ok = BleControl::otaEnd(msg);
      say(msg);
      if (ok) {
        BleControl::publishState(stateJson(now, 0, 0));
        Lighting::setLevel(dusk::Output::Interior, 0);
        Lighting::setLevel(dusk::Output::Exterior, 0);
        delay(1500);                     // laisse partir le message vers le téléphone
        ESP.restart();
      }
    } else if (cmd == "ota abort") {
      BleControl::otaAbort();
      say("mise à jour annulée");
    } else if (cmd.startsWith("set ") || cmd == "reset") {
      say(Settings::apply(cmd, cfg));
      controller.setConfig(cfg);
    } else {
      say("commande inconnue : " + cmd);
    }
    BleControl::setInfo(infoJson());
  }
}

void setup() {
  StatusLed::begin();
  StatusLed::on();                 // LED allumée pendant le démarrage : la carte est vivante

  Serial.begin(SERIAL_BAUD);
  delay(SERIAL_WAIT_MS);           // laisse le Mac reconnaître le port USB

  // Compteur de démarrages, gardé en mémoire flash : des redémarrages
  // fréquents trahissent un accu trop faible ou un faux contact.
  Preferences p;
  p.begin("sante", false);
  boots = p.getUInt("boots", 0) + 1;
  p.putUInt("boots", boots);
  p.end();
  resetReason = reasonName(esp_reset_reason());

  cfg = Settings::load();
  controller.setConfig(cfg);

  Serial.println();
  Serial.println(F("=== Éclairage poulailler ==="));
  Serial.printf("Programme du %s, cœur Arduino ESP32 %d.%d.%d\n", FIRMWARE,
                ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
  Serial.printf("Démarrage n° %lu (%s)\n", (unsigned long)boots, resetReason);
  if (ENABLE_LIGHTING_TEST) {
    Serial.println(F("Mode : banc d'essai du fondu (ENABLE_LIGHTING_TEST)"));
  } else {
    Serial.printf("Mode : crépuscule, %s\n", FAST_TEST ? "TEMPS ACCÉLÉRÉ" : "durées réelles");
    Serial.printf("Seuils : crépuscule <= %u, jour >= %u\n", cfg.duskThreshold, cfg.dayThreshold);
  }

  LightSensor::begin();
  Lighting::begin();
  if (ENABLE_BLE) {
    BleControl::begin();
    BleControl::setInfo(infoJson());
    Serial.printf("Bluetooth : visible sous le nom « %s »\n", BLE_NAME);
  }

  StatusLed::off();
  Serial.println(F("brut | lissé | phase            | int. | ext."));
}

void loop() {
  const uint32_t now = millis();
  StatusLed::update();

  if (ENABLE_LIGHTING_TEST) {
    Lighting::updateFadeTest();
    return;
  }

  // Mesure périodique
  bool measured = false;
  if (now - lastMeasure >= LIGHT_PERIOD_MS) {
    lastMeasure = now;
    measured = true;
    LightSensor::Reading r = LightSensor::read();
    lastRaw = r.raw;
    const bool changed = controller.update(now, r.raw);
    if (changed) printTransition(now);
    Serial.printf("%4u | %5.0f | ", r.raw, controller.smoothed());
    {
      const char* name = dusk::phaseName(controller.phase());
      int letters = 0;                           // aligne malgré les accents UTF-8
      for (const char* q = name; *q; ++q) if ((*q & 0xC0) != 0x80) ++letters;
      Serial.print(name);
      for (int i = letters; i < 16; ++i) Serial.print(' ');
    }
    Serial.printf(" | %3d%% | %3d%%\n",
                  pct(controller.level(dusk::Output::Interior, now)),
                  pct(controller.level(dusk::Output::Exterior, now)));
    StatusLed::flash(STATUS_BLINK_ON_MS);
  }

  // Phases minutées entre deux mesures (fin d'allumage, fin de fondu)
  if (controller.tick(now)) printTransition(now);

  // Commandes Bluetooth
  bool publishNow = false;
  if (ENABLE_BLE) {
    String cmd;
    while (BleControl::pollCommand(cmd)) {
      handleCommand(cmd, now);
      publishNow = true;
    }
  }

  // Sorties : cycle automatique, remplacé par la pleine puissance si allumage manuel
  float in = controller.level(dusk::Output::Interior, now);
  float out = controller.level(dusk::Output::Exterior, now);
  if (manualActive(dusk::Output::Interior, now)) in = 1.0f;
  if (manualActive(dusk::Output::Exterior, now)) out = 1.0f;
  Lighting::setLevel(dusk::Output::Interior, in);
  Lighting::setLevel(dusk::Output::Exterior, out);

  // Courbe des 24 h
  if (measured && History::sample(controller.smoothed(), in > 0.0f, out > 0.0f) && ENABLE_BLE) {
    refreshCurve();
  }

  // Téléphone : état à chaque mesure et après une commande, infos toutes les 30 s
  if (ENABLE_BLE) {
    if (measured || publishNow) BleControl::publishState(stateJson(now, in, out));
    static uint32_t lastInfo = 0;
    if (now - lastInfo >= 30000) {
      lastInfo = now;
      BleControl::setInfo(infoJson());
    }
  }

  static bool witnessOn = false;
  if (in > 0.0f || out > 0.0f) {
    StatusLed::on();
    witnessOn = true;
  } else if (witnessOn) {
    StatusLed::off();
    witnessOn = false;
  }
}
