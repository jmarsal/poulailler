// main.cpp
// Détection du crépuscule et éclairage du soir, sur accu et panneau solaire.
// La logique est dans lib/dusk_controller (testée sur le Mac) ; ce fichier
// relie le capteur, la logique, les sorties PWM, la LED, le Bluetooth, l'accu
// et le sommeil profond.
//
// Deux sorties : ruban intérieur (GPIO10) et ruban extérieur (GPIO4).
//
// Sommeil profond (ENABLE_DEEP_SLEEP) : hors soirée, la carte se réveille toutes
// les 30 s, mesure, enregistre, et se rendort. Chaque réveil repart de setup() :
// tout ce qui doit durer (phase, lissage, courbe, horloge) est gardé en mémoire
// RTC. Elle reste éveillée pendant la soirée, pendant les fenêtres Bluetooth
// (20 s toutes les 5 min) et tant qu'un téléphone est connecté.
//
// Bluetooth (ble_control) : allumage manuel (15 min au plus), soirée lancée à la
// main, réglages (settings), courbe et journal (history, datalog), heure réelle,
// santé de la carte, mise à jour du programme.

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_sleep.h>
#include <esp_attr.h>
#include <driver/gpio.h>
#include <new>
#include "config.h"
#include "clock.h"
#include "status_led.h"
#include "light_sensor.h"
#include "lighting.h"
#include "dusk_controller.h"
#include "ble_control.h"
#include "settings.h"
#include "history.h"
#include "datalog.h"
#include "power_monitor.h"

namespace {
  constexpr const char* FIRMWARE = "2026-10-21 h";

  // ---- État gardé pendant le sommeil profond (mémoire RTC) ----------------
  // Le contrôleur est un objet C++ : son constructeur ne doit tourner qu'à la
  // mise sous tension, pas à chaque réveil. On le range donc dans une zone RTC
  // non initialisée, avec un marqueur de validité.
  constexpr uint32_t RTC_MAGIC = 0x50C0A11E;
  RTC_NOINIT_ATTR uint32_t rtcMagic;
  RTC_NOINIT_ATTR alignas(8) uint8_t ctrlMem[sizeof(dusk::Controller)];
  dusk::Controller& controller = *reinterpret_cast<dusk::Controller*>(ctrlMem);

  RTC_DATA_ATTR uint32_t lastMeasure = 0;     // instant de la dernière mesure (monoMs)
  RTC_DATA_ATTR uint32_t nextWindow = 0;      // prochaine fenêtre Bluetooth (monoMs)
  RTC_DATA_ATTR uint16_t lastRaw = 0;
  RTC_DATA_ATTR uint32_t wakes = 0;           // réveils depuis la mise sous tension
  RTC_DATA_ATTR uint8_t  battGuard = 0;       // 0 normal, 1 intérieur bridé, 2 rien (soirée en cours)
  RTC_DATA_ATTR int      coldReason = 0;      // cause du dernier vrai démarrage (pas un réveil)

  dusk::Config cfg = DUSK_CONFIG;

  // ---- État de cet éveil seulement -----------------------------------------
  bool     manualOn[2]    = {false, false};   // 0 = intérieur, 1 = extérieur
  uint32_t manualStart[2] = {0, 0};
  bool     skipExterior   = false;            // soirée lancée « intérieur seulement »
  bool     bleOn          = false;
  uint32_t stayAwakeUntil = 0;                // fin de la fenêtre Bluetooth en cours (monoMs)

  uint32_t boots = 0;
  const char* resetReason = "?";
  String lastMsg = "";
  uint16_t msgCount = 0;

  uint32_t now32() { return (uint32_t)Clock::monoMs(); }
  int idx(dusk::Output o) { return o == dusk::Output::Interior ? 0 : 1; }
  int pct(float v) { return (int)(v * 100 + 0.5f); }
  bool evening() { return controller.phase() == dusk::Phase::Evening; }

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
      case ESP_RST_POWERON:   return "mise sous tension";
      case ESP_RST_SW:        return "redémarrage logiciel";
      case ESP_RST_PANIC:     return "plantage";
      case ESP_RST_INT_WDT:
      case ESP_RST_TASK_WDT:
      case ESP_RST_WDT:       return "chien de garde";
      case ESP_RST_BROWNOUT:  return "tension trop basse";
      case ESP_RST_DEEPSLEEP: return "réveil";
      default:                return "autre";
    }
  }

  String clockText() {
    if (!Clock::epochKnown()) return "";
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

  // Accu au moment où la soirée commence (rubans encore éteints) : règle de la soirée.
  void decideBatteryGuard() {
    battGuard = 0;
    const PowerMonitor::Reading b = PowerMonitor::last();
    if (!b.ok) return;
    if (b.mv < BATT_CRITICAL_MV) battGuard = 2;
    else if (b.mv < BATT_LOW_MV) battGuard = 1;
    if (battGuard) say(battGuard == 2 ? "accu trop bas : soirée sans éclairage"
                                      : "accu bas : intérieur bridé ce soir");
  }

  void printTransition(uint32_t now) {
    String clk = clockText();
    Serial.printf("---- %s (à %lu s%s%s)\n", dusk::phaseName(controller.phase()),
                  (unsigned long)(now / 1000), clk.length() ? ", " : "", clk.c_str());
    if (evening()) { History::eveningStart(controller.smoothed()); decideBatteryGuard(); }
    if (controller.phase() == dusk::Phase::Night) History::eveningEnd();
  }

  // État courant, compact : il doit tenir dans une notification Bluetooth.
  String stateJson(uint32_t now, float in, float out) {
    char buf[260];
    String m = lastMsg;
    m.replace("\"", "'");
    const PowerMonitor::Reading b = PowerMonitor::last();
    snprintf(buf, sizeof(buf),
             "{\"p\":%d,\"r\":%u,\"s\":%.0f,\"i\":%d,\"e\":%d,\"mi\":%d,\"me\":%d,\"u\":%lu,\"c\":\"%s\",\"t\":%lu,"
             "\"b\":%u,\"a\":%d,\"g\":%u,\"n\":%u,\"o\":%u,\"m\":\"%s\"}",
             (int)controller.phase(), lastRaw, controller.smoothed(), pct(in), pct(out),
             manualMinutesLeft(0, now), manualMinutesLeft(1, now),
             (unsigned long)Clock::monoSec(), clockText().c_str(), (unsigned long)Clock::epoch(),
             b.ok ? b.mv : 0, b.ok ? b.ma : 0, battGuard, msgCount,
             (unsigned)BleControl::otaReceived(), m.c_str());
    return String(buf);
  }

  // Réglages, santé et journal : lus à la demande par le téléphone.
  String infoJson() {
    char head[300];
    snprintf(head, sizeof(head),
             "{\"ver\":\"%s\",\"mode\":\"%s\",\"boots\":%lu,\"rst\":\"%s\",\"ev\":%u,\"t\":%.1f,\"heap\":%u,\"u\":%lu,"
             "\"sleep\":%d,\"wk\":%lu,\"ina\":%d,",
             FIRMWARE, FAST_TEST ? "test" : "reel", (unsigned long)boots, resetReason,
             History::eveningsCount(), temperatureRead(), (unsigned)(ESP.getFreeHeap() / 1024),
             (unsigned long)Clock::monoSec(), ENABLE_DEEP_SLEEP ? 1 : 0, (unsigned long)wakes,
             PowerMonitor::present() ? 1 : 0);
    char lg[48];
    snprintf(lg, sizeof(lg), "\"lg\":[%lu,%lu,%d],", (unsigned long)DataLog::firstSeq(),
             (unsigned long)DataLog::nextSeq(), DataLog::ok() ? 1 : 0);
    return String(head) + lg + "\"cfg\":" + Settings::toJson(cfg) + ",\"j\":" + History::journalJson() + "}";
  }

  void refreshCurve() {
    static uint8_t buf[8 + 2 * HISTORY_POINTS];
    size_t n = History::curveBytes(buf, sizeof(buf));
    if (n) BleControl::setCurve(buf, n);
  }

  void startBle(uint32_t now, uint32_t forMs) {
    if (!ENABLE_BLE) return;
    if (!bleOn) {
      BleControl::begin();
      refreshCurve();
      BleControl::setInfo(infoJson());
      bleOn = true;
    }
    if ((int32_t)(now + forMs - stayAwakeUntil) > 0) stayAwakeUntil = now + forMs;
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
    else if (cmd == "soiree" || cmd == "soiree int") {
      // Bouton « Lancer la soirée » : la porte est fermée mais rien ne s'est allumé.
      manualOn[0] = manualOn[1] = false;
      skipExterior = (cmd == "soiree int");
      controller.startEvening(now);
      printTransition(now);              // journal des soirées, règle de l'accu
      say(skipExterior ? "soirée lancée à la main, intérieur seulement" : "soirée lancée à la main");
    }
    else if (cmd.startsWith("time ")) {
      // Heure donnée par le téléphone, en secondes depuis 1970 (UTC).
      long e = cmd.substring(5).toInt();
      if (e > 1700000000L) {
        Clock::setEpoch((uint32_t)e);
        DataLog::timeKnown((uint32_t)e, Clock::monoSec());
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
        DataLog::flush();
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

  // Sorties à 0 et broches maintenues pendant le sommeil : un D4184 dont l'entrée
  // flotterait pourrait s'allumer, et la porte resterait ouverte.
  constexpr gpio_num_t HELD[] = {(gpio_num_t)PIN_PWM_INTERIOR, (gpio_num_t)PIN_PWM_EXTERIOR,
                                 (gpio_num_t)PIN_LIGHT_POWER, (gpio_num_t)PIN_STATUS_LED};

  void releaseHeldPins() {
    for (gpio_num_t p : HELD) gpio_hold_dis(p);
    gpio_deep_sleep_hold_dis();
  }

  void goToSleep(uint32_t now) {
    uint32_t sleepMs = (uint32_t)(lastMeasure + LIGHT_PERIOD_MS - now);
    if ((int32_t)sleepMs < 200) sleepMs = 200;
    Lighting::setLevel(dusk::Output::Interior, 0);
    Lighting::setLevel(dusk::Output::Exterior, 0);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcDetach(PIN_PWM_INTERIOR); ledcDetach(PIN_PWM_EXTERIOR);
#else
    ledcDetachPin(PIN_PWM_INTERIOR); ledcDetachPin(PIN_PWM_EXTERIOR);
#endif
    pinMode(PIN_PWM_INTERIOR, OUTPUT); digitalWrite(PIN_PWM_INTERIOR, LOW);
    pinMode(PIN_PWM_EXTERIOR, OUTPUT); digitalWrite(PIN_PWM_EXTERIOR, LOW);
    digitalWrite(PIN_LIGHT_POWER, LOW);
    StatusLed::off();
    for (gpio_num_t p : HELD) gpio_hold_en(p);
    gpio_deep_sleep_hold_en();
    if (Serial) Serial.flush();
    esp_sleep_enable_timer_wakeup((uint64_t)sleepMs * 1000ULL);
    Clock::beforeSleep(sleepMs);
    esp_deep_sleep_start();
  }
}

void setup() {
  const bool wokeUp = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
  const bool cold = !wokeUp || rtcMagic != RTC_MAGIC;
  releaseHeldPins();
  Clock::begin(cold);

  StatusLed::begin();
  Serial.begin(SERIAL_BAUD);
  Serial.setTxTimeoutMs(0);        // sans Mac branché, ne jamais attendre le port USB
  if (cold) {
    StatusLed::on();               // LED allumée pendant le démarrage : la carte est vivante
    delay(SERIAL_WAIT_MS);         // laisse le Mac reconnaître le port USB
    new (ctrlMem) dusk::Controller(DUSK_CONFIG);
    rtcMagic = RTC_MAGIC;
    lastMeasure = now32() - LIGHT_PERIOD_MS;   // première mesure tout de suite
    nextWindow = 0;
    wakes = 0;
    battGuard = 0;
    coldReason = (int)esp_reset_reason();
  }
  ++wakes;

  // Compteur de démarrages (mises sous tension seulement), gardé en flash :
  // des redémarrages fréquents trahissent un accu trop faible ou un faux contact.
  Preferences p;
  p.begin("sante", !cold);
  boots = cold ? p.getUInt("boots", 0) + 1 : p.getUInt("boots", 1);
  if (cold) p.putUInt("boots", boots);
  p.end();
  resetReason = reasonName((esp_reset_reason_t)coldReason);

  cfg = Settings::load();
  controller.setConfig(cfg);

  if (cold) {
    Serial.println();
    Serial.println(F("=== Éclairage poulailler ==="));
    Serial.printf("Programme du %s, cœur Arduino ESP32 %d.%d.%d\n", FIRMWARE,
                  ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
    Serial.printf("Démarrage n° %lu (%s)\n", (unsigned long)boots, resetReason);
    Serial.printf("Mode : crépuscule, %s%s\n", FAST_TEST ? "TEMPS ACCÉLÉRÉ" : "durées réelles",
                  ENABLE_DEEP_SLEEP ? ", sommeil profond" : "");
    Serial.printf("Seuils : crépuscule <= %u, jour >= %u\n", cfg.duskThreshold, cfg.dayThreshold);
  }

  DataLog::begin((uint16_t)boots);
  LightSensor::begin();
  Lighting::begin();
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);   // appui = niveau bas
  if (PowerMonitor::begin() && cold) Serial.println(F("Accu : INA219 trouvé"));

  const uint32_t now = now32();
  if (cold) startBle(now, BLE_AFTER_BOOT_MS);           // installation, câble, mise à jour
  if (evening()) startBle(now, 0);                       // réveil en pleine soirée (rare)
  if (cold) {
    StatusLed::off();
    Serial.println(F("brut | lissé | phase            | int. | ext. | accu"));
  }
}

void loop() {
  const uint32_t now = now32();
  StatusLed::update();

  if (ENABLE_LIGHTING_TEST) {
    Lighting::updateFadeTest();
    return;
  }

  // Mesure périodique (au réveil, elle est due : c'est pour elle qu'on s'est réveillé)
  bool measured = false;
  if (now - lastMeasure >= LIGHT_PERIOD_MS) {
    lastMeasure = now;
    measured = true;
    PowerMonitor::read();
    LightSensor::Reading r = LightSensor::read();
    lastRaw = r.raw;
    const bool changed = controller.update(now, r.raw);
    if (changed) printTransition(now);
    if (Serial) {
      const PowerMonitor::Reading b = PowerMonitor::last();
      const char* name = dusk::phaseName(controller.phase());
      int letters = 0;                           // aligne malgré les accents UTF-8
      for (const char* q = name; *q; ++q) if ((*q & 0xC0) != 0x80) ++letters;
      Serial.printf("%4u | %5.0f | %s", r.raw, controller.smoothed(), name);
      for (int i = letters; i < 16; ++i) Serial.print(' ');
      Serial.printf(" | %3d%% | %3d%% | %s\n",
                    pct(controller.level(dusk::Output::Interior, now)),
                    pct(controller.level(dusk::Output::Exterior, now)),
                    b.ok ? (String(b.mv) + " mV " + String(b.ma) + " mA").c_str() : "-");
    }
    if (!ENABLE_DEEP_SLEEP) StatusLed::flash(STATUS_BLINK_ON_MS);
  }

  // Phases minutées entre deux mesures (fin d'allumage, fin de fondu)
  if (controller.tick(now)) printTransition(now);

  // Bluetooth : pendant la soirée, et une fenêtre de temps en temps
  if (evening()) startBle(now, 0);
  if (ENABLE_DEEP_SLEEP && (int32_t)(now - nextWindow) >= 0) {
    nextWindow = now + BLE_WINDOW_EVERY_MS;
    startBle(now, BLE_WINDOW_MS);
  }

  // Commandes Bluetooth
  bool publishNow = false;
  if (bleOn) {
    String cmd;
    while (BleControl::pollCommand(cmd)) {
      handleCommand(cmd, now);
      publishNow = true;
    }
  }

  // Appui long sur BOOT (carte éveillée) : efface les appairages Bluetooth
  if (bleOn) {
    static uint32_t pressedSince = 0;
    static bool done = false;
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
      if (pressedSince == 0) pressedSince = now ? now : 1;
      if (!done && now - pressedSince >= BOOT_HOLD_MS) {
        done = true;
        BleControl::clearBonds();
        say("appairages effacés : oublie l'appareil sur le téléphone, puis reconnecte-toi");
        Serial.println(F("---- appairages Bluetooth effacés (bouton BOOT)"));
        StatusLed::flash(1500);
        publishNow = true;
      }
    } else {
      pressedSince = 0;
      done = false;
    }
  }

  // Sorties : cycle automatique, règle de l'accu, puis allumage manuel s'il y en a un
  float in = controller.level(dusk::Output::Interior, now);
  float out = controller.level(dusk::Output::Exterior, now);
  if (!evening()) { skipExterior = false; battGuard = 0; }
  if (skipExterior) out = 0.0f;
  if (battGuard == 1 && in > BATT_LOW_INTERIOR) in = BATT_LOW_INTERIOR;
  if (battGuard == 2) in = out = 0.0f;
  if (evening() && measured) {
    const PowerMonitor::Reading b = PowerMonitor::last();
    if (b.ok && b.mv < BATT_CUTOFF_MV && battGuard != 2) {
      battGuard = 2;                               // l'accu s'écroule sous la charge : on coupe
      say("accu à bout : éclairage coupé");
    }
  }
  if (manualActive(dusk::Output::Interior, now)) in = 1.0f;
  if (manualActive(dusk::Output::Exterior, now)) out = 1.0f;
  Lighting::setLevel(dusk::Output::Interior, in);
  Lighting::setLevel(dusk::Output::Exterior, out);

  // Courbe des 24 h et journal longue durée
  if (measured && History::sample(controller.smoothed(), in > 0.0f, out > 0.0f)) {
    if (bleOn) refreshCurve();
    const float sm = controller.smoothed();
    const uint16_t v = sm < 0 ? 0 : sm > 4095 ? 4095 : (uint16_t)(sm + 0.5f);
    const uint16_t packed = v | (in > 0.0f ? 0x1000 : 0) | (out > 0.0f ? 0x2000 : 0);
    const PowerMonitor::Reading b = PowerMonitor::last();
    DataLog::add(Clock::epoch(), Clock::monoSec(), packed,
                 b.ok ? b.mv : 0, b.ok ? b.ma : DataLog::NO_CURRENT);
  }

  // Téléphone : état à chaque mesure et après une commande, infos toutes les 30 s
  if (bleOn) {
    if (measured || publishNow) BleControl::publishState(stateJson(now, in, out));
    static uint32_t lastInfo = 0;
    if (now - lastInfo >= 30000) {
      lastInfo = now;
      BleControl::setInfo(infoJson());
    }
  }

  // Témoin : LED bleue allumée tant qu'un ruban est allumé
  static bool witnessOn = false;
  if (in > 0.0f || out > 0.0f) {
    StatusLed::on();
    witnessOn = true;
  } else if (witnessOn) {
    StatusLed::off();
    witnessOn = false;
  }

  // Dodo, si plus rien ne retient la carte éveillée
  if (ENABLE_DEEP_SLEEP) {
    const bool busy = evening() || manualOn[0] || manualOn[1] || in > 0.0f || out > 0.0f
                      || BleControl::connected() || BleControl::otaActive()
                      || (bleOn && (int32_t)(stayAwakeUntil - now) > 0)
                      || digitalRead(BOOT_BUTTON_PIN) == LOW;
    if (!busy) goToSleep(now);
  }
  delay(10);   // laisse respirer la tâche Bluetooth
}
