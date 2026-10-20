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

uint32_t Controller::eveningMs() const {
  const uint32_t in = cfg_.interior.holdMs + cfg_.interior.fadeOutMs;
  const uint32_t out = cfg_.exterior.holdMs + cfg_.exterior.fadeOutMs;
  return in > out ? in : out;
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
    case Phase::Day:
      if (confirmed(smooth_ <= cfg_.duskThreshold, nowMs, cfg_.duskConfirmMs)) {
        triggerAt_ = nowMs;
        enter(Phase::Evening, nowMs);
      }
      break;
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

float Controller::level(Output out, uint32_t nowMs) const {
  if (phase_ != Phase::Evening) return 0.0f;
  const Profile& p = out == Output::Interior ? cfg_.interior : cfg_.exterior;
  return profileLevel(p, nowMs - triggerAt_);
}

}  // namespace dusk
