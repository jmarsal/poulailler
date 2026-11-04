#include "dusk_controller.h"

namespace dusk {

const char* phaseName(Phase p) {
  switch (p) {
    case Phase::WaitDay: return "attente du jour";
    case Phase::Day:     return "jour";
    case Phase::Evening: return "soirée";
    case Phase::Night:   return "nuit";
  }
  return "?";
}

float profileLevel(const Profile& p, uint32_t elapsed) {
  float f;
  if (p.fadeInMs > 0 && elapsed < p.fadeInMs) {
    f = (float)elapsed / (float)p.fadeInMs;
  } else if (elapsed < p.holdMs) {
    f = 1.0f;
  } else if (p.fadeOutMs > 0 && elapsed < p.holdMs + p.fadeOutMs) {
    f = 1.0f - (float)(elapsed - p.holdMs) / (float)p.fadeOutMs;
  } else {
    f = 0.0f;
  }
  return f * p.maxLevel;
}

Controller::Controller(const Config& cfg) : cfg_(cfg) {}

Profile Controller::effective(Output out) const {
  Profile p = out == Output::Interior ? cfg_.interior : cfg_.exterior;
  p.holdMs += extensionMs(out);
  return p;
}

uint32_t Controller::eveningMs() const {
  const Profile pin = effective(Output::Interior), pout = effective(Output::Exterior);
  const uint32_t in = pin.holdMs + pin.fadeOutMs;
  const uint32_t out = extDelayMs_ + pout.holdMs + pout.fadeOutMs;
  return in > out ? in : out;
}

void Controller::trigger(uint32_t nowMs, const SunClock& sun) {
  triggerAt_ = nowMs;
  extExtMs_ = intExtMs_ = extDelayMs_ = 0;
  if (useSun(sun)) {
    // Entrée retardée jusqu'à coucher + N (jamais si la soirée part après).
    if (solar_.exteriorStartMin > 0) {
      const int64_t wait = ((int64_t)solar_.exteriorStartMin * 60 - sun.secFromSunset) * 1000;
      if (wait > 0) extDelayMs_ = (uint32_t)wait;
    }
    // Combien de temps chaque sortie doit tenir pour atteindre coucher + N.
    const int64_t maxMs = (int64_t)solar_.maxExtensionMin * 60000;
    auto extension = [&](int16_t untilMin, uint32_t delayMs, uint32_t holdMs) -> uint32_t {
      const int64_t wanted = ((int64_t)untilMin * 60 - sun.secFromSunset) * 1000 - delayMs - holdMs;
      if (wanted <= 0) return 0;
      return (uint32_t)(wanted < maxMs ? wanted : maxMs);
    };
    extExtMs_ = extension(solar_.exteriorUntilMin, extDelayMs_, cfg_.exterior.holdMs);
    intExtMs_ = extension(solar_.interiorFullUntilMin, 0, cfg_.interior.holdMs);
  }
  enter(Phase::Evening, nowMs);
}

void Controller::enter(Phase p, uint32_t nowMs) {
  phase_ = p;
  phaseStart_ = nowMs;
  condActive_ = false;   // toute confirmation en cours repart de zéro
}

bool Controller::confirmed(bool cond, uint32_t nowMs, uint32_t durationMs) {
  if (!cond) {
    condActive_ = false;
    return false;
  }
  if (!condActive_) {
    condActive_ = true;
    condSince_ = nowMs;
  }
  // Différences non signées : restent justes quand millis() repasse par zéro.
  return (uint32_t)(nowMs - condSince_) >= durationMs;
}

bool Controller::update(uint32_t nowMs, uint16_t raw) {
  return update(nowMs, raw, SunClock{false, 0});
}

bool Controller::update(uint32_t nowMs, uint16_t raw, const SunClock& sun) {
  // Lissage exponentiel : chaque mesure tire la moyenne vers elle,
  // d'autant plus fort que le temps écoulé est long devant la constante.
  if (!haveSample_) {
    smooth_ = raw;
    haveSample_ = true;
    phaseStart_ = nowMs;
  } else {
    uint32_t dt = nowMs - lastSample_;
    float alpha = cfg_.smoothingMs == 0 ? 1.0f : (float)dt / (float)cfg_.smoothingMs;
    if (alpha > 1.0f) alpha = 1.0f;
    smooth_ += alpha * ((float)raw - smooth_);
  }
  lastSample_ = nowMs;

  const Phase before = phase_;

  switch (phase_) {
    case Phase::WaitDay:
    case Phase::Night: {
      const bool unlocked =
          phase_ == Phase::WaitDay || (uint32_t)(nowMs - triggerAt_) >= cfg_.minNightMs;
      if (!unlocked) {
        condActive_ = false;
        break;
      }
      if (confirmed(smooth_ >= cfg_.dayThreshold, nowMs, cfg_.dayConfirmMs)) {
        enter(Phase::Day, nowMs);
      }
      break;
    }
    case Phase::Day: {
      // Le noir se confirme comme d'habitude, même avant la fenêtre : si un
      // orage dure jusqu'à son ouverture, la soirée part aussitôt.
      const bool dark = confirmed(smooth_ <= cfg_.duskThreshold, nowMs, cfg_.duskConfirmMs);
      const bool sunKnown = useSun(sun);
      const bool windowOpen = !sunKnown || sun.secFromSunset >= -(int32_t)solar_.windowBeforeMin * 60;
      const bool fallback = sunKnown
          && sun.secFromSunset >= (int32_t)solar_.fallbackAfterMin * 60
          && sun.secFromSunset <= (int32_t)solar_.windowAfterMin * 60;
      if ((dark && windowOpen) || fallback) trigger(nowMs, sun);
      break;
    }
    case Phase::Evening:
      break;   // minutée, gérée par tick()
  }

  tick(nowMs);
  return phase_ != before;
}

bool Controller::tick(uint32_t nowMs) {
  if (phase_ == Phase::Evening && (uint32_t)(nowMs - triggerAt_) >= eveningMs()) {
    enter(Phase::Night, triggerAt_ + eveningMs());
    return true;
  }
  return false;
}

void Controller::startEvening(uint32_t nowMs) {
  // Soirée à la main : durées réglées telles quelles, sans allongement.
  trigger(nowMs, SunClock{false, 0});
}

float Controller::level(Output out, uint32_t nowMs) const {
  if (phase_ != Phase::Evening) return 0.0f;
  const uint32_t elapsed = nowMs - triggerAt_, wait = delayMs(out);
  if (elapsed < wait) return 0.0f;   // entrée pas encore allumée
  return profileLevel(effective(out), elapsed - wait);
}

}  // namespace dusk
