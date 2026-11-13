#include "performance_envelope.h"
#include "doctest.h"

TEST_CASE("Performance envelope attacks, sustains and releases smoothly") {
  PerformanceEnvelope envelope;
  CHECK(envelope.output() == doctest::Approx(0.0f));
  envelope.gate(true);
  CHECK(envelope.advance(75, 1000) == doctest::Approx(0.5f));
  CHECK(envelope.advance(75, 1000) == doctest::Approx(1.0f));
  CHECK(envelope.phase() == PerformanceEnvelope::Phase::sustain);
  CHECK(envelope.advance(1000, 1000) == doctest::Approx(1.0f));
  envelope.gate(false);
  CHECK(envelope.advance(200, 1000) == doctest::Approx(0.5f));
  CHECK(envelope.advance(200, 1000) == doctest::Approx(0.0f));
  CHECK(envelope.phase() == PerformanceEnvelope::Phase::idle);
}

TEST_CASE("Performance envelope releases from its current attack level and retriggers continuously") {
  PerformanceEnvelope envelope;
  envelope.gate(true);
  const float beforeRelease = envelope.advance(60, 1000);
  envelope.gate(false);
  CHECK(envelope.output() == doctest::Approx(beforeRelease));
  const float duringRelease = envelope.advance(100, 1000);
  CHECK(duringRelease < beforeRelease);
  envelope.gate(true);
  CHECK(envelope.output() == doctest::Approx(duringRelease));
  CHECK(envelope.advance(75, 1000) > duringRelease);
  CHECK(envelope.held());
}

TEST_CASE("Performance envelope gate event order and held duplicates are deterministic") {
  PerformanceEnvelope envelope;
  envelope.gate(true);
  envelope.gate(true); // duplicate MIDI Note On does not restart the ramp
  CHECK(envelope.advance(75, 1000) == doctest::Approx(0.5f));
  envelope.gate(false);
  envelope.gate(false); // duplicate Note Off does not restart the release
  CHECK(envelope.advance(200, 1000) == doctest::Approx(0.25f));
  envelope.gate(true); // rapid re-press starts from current value
  CHECK(envelope.advance(75, 1000) == doctest::Approx(0.625f));
}

TEST_CASE("Performance delay overlay is bounded and restores the underlying send") {
  PerformanceEnvelope envelope;
  const int underlyingSend = 70;
  envelope.gate(true);
  envelope.advance(150, 1000);
  const int raised = envelope.applyPercentOffset(underlyingSend, 60);
  CHECK(raised == 100);
  CHECK(envelope.applyPercentOffset(100, 60) == 100);
  envelope.gate(false);
  envelope.advance(400, 1000);
  const int restored = envelope.applyPercentOffset(underlyingSend, 60);
  CHECK(restored == underlyingSend);
  CHECK(envelope.output() >= 0.0f);
  CHECK(envelope.output() <= 1.0f);
}

TEST_CASE("Performance envelope is independent of instrument and chain restarts") {
  PerformanceEnvelope envelope;
  envelope.gate(true);
  envelope.advance(90, 1000);
  const float beforeChainChange = envelope.output();
  // Playback chain/instrument changes do not reset this dedicated runtime state.
  CHECK(envelope.output() == doctest::Approx(beforeChainChange));
  CHECK(envelope.advance(60, 1000) == doctest::Approx(1.0f));
}
