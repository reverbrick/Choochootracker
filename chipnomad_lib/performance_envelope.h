#ifndef CHIPNOMAD_PERFORMANCE_ENVELOPE_H
#define CHIPNOMAD_PERFORMANCE_ENVELOPE_H

// Runtime-only, audio-thread-owned envelope for momentary performance controls.
// Linear segments are deliberately simple and do not allocate or depend on
// note/instrument lifetime.
class PerformanceEnvelope {
 public:
  enum class Phase { idle, attack, sustain, release };

  void gate(bool pressed) {
    if (pressed) {
      if (held_) return;
      held_ = true;
      begin(Phase::attack, 1.0f, 0.150f);
    } else {
      if (!held_) return;
      held_ = false;
      begin(Phase::release, 0.0f, 0.400f);
    }
  }

  float advance(int frames, int sampleRate) {
    if (frames <= 0 || sampleRate <= 0) return level_;
    if (phase_ == Phase::attack || phase_ == Phase::release) {
      elapsed_ += static_cast<float>(frames) / static_cast<float>(sampleRate);
      const float rawProgress = elapsed_ / duration_;
      const float progress = rawProgress < 1.0f ? rawProgress : 1.0f;
      level_ = start_ + (target_ - start_) * progress;
      if (progress >= 1.0f) {
        if (phase_ == Phase::attack) phase_ = Phase::sustain;
        else phase_ = Phase::idle;
      }
    }
    return level_;
  }

  float output() const { return level_; }
  int applyPercentOffset(int base, int maximumOffset) const {
    int result = base + static_cast<int>(level_ * maximumOffset + 0.5f);
    if (result < 0) return 0;
    if (result > 100) return 100;
    return result;
  }
  bool held() const { return held_; }
  Phase phase() const { return phase_; }

 private:
  void begin(Phase phase, float target, float duration) {
    phase_ = phase;
    start_ = level_;
    target_ = target;
    duration_ = duration;
    elapsed_ = 0.0f;
  }

  float level_ = 0.0f;
  float start_ = 0.0f;
  float target_ = 0.0f;
  float duration_ = 0.0f;
  float elapsed_ = 0.0f;
  Phase phase_ = Phase::idle;
  bool held_ = false;
};

#endif
