#include "doctest.h"
#include "chipnomad_lib.h"
#include "midi/midi_router.h"
#include "midi/midi_recording.h"
#include "pitch_table_utils.h"
#include "project_utils.h"
#include "synth/sample_voice.h"
#define restrict __restrict
extern "C" {
#include "orca/sim.h"
#include "orca/vmio.h"
void mbuffer_clear(Mark* mbuffer, Usz height, Usz width);
}
#undef restrict

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {
std::vector<MidiEvent> incoming;
size_t incomingIndex;
bool queueAccepted;

int pollSyntheticMidi(void*, MidiEvent* event) {
  if (incomingIndex >= incoming.size()) return 0;
  *event = incoming[incomingIndex++];
  return 1;
}

uint64_t syntheticNow(void*) { return 1000000; }
void discardMidiOutput(void*, const MidiEvent*, uint64_t) {}
unsigned int noDroppedMidi(void*) { return 0; }

const MidiBackend syntheticBackend = {
  nullptr,
  nullptr, nullptr, nullptr, nullptr,
  nullptr, nullptr, nullptr, nullptr,
  pollSyntheticMidi,
  discardMidiOutput,
  nullptr,
  noDroppedMidi,
  syntheticNow,
  nullptr, nullptr,
  1, 1, 0, 0,
};

void routeIntentToEngine(void* userdata, const MidiPreviewIntent& intent) {
  auto* state = static_cast<ChipNomadState*>(userdata);
  queueAccepted = chipnomadQueueMidiInput(state, intent.track, intent.note,
    static_cast<uint8_t>(intent.instrument), intent.stop ? 0 : intent.velocity,
    intent.timestampMicros, 0, MidiRecordMode::quantized) != 0;
}

void enqueue(uint8_t type, uint8_t channel, uint8_t note, uint8_t velocity) {
  incoming.push_back({1000000, type, channel, note, velocity});
}

bool pollMidi(ChipNomadState* state, MidiRouterState* router, int instrument) {
  incomingIndex = 0;
  queueAccepted = false;
  midiRouterProcessInput(router, instrument, 0, -1, routeIntentToEngine, state);
  incoming.clear();
  return queueAccepted;
}
}

TEST_CASE("synthetic MIDI input plays and releases a sample through the normal audio path") {
  midiRouterSetBackend(&syntheticBackend);
  auto* state = chipnomadCreate();
  REQUIRE(state);
  auto& project = state->project;
  project.tickRate = 48;
  project.chipType = ChipType::AY;
  project.chipsCount = 1;
  project.tracksCount = projectGetTotalTracks(&project);
  calculatePitchTableAY(&project);

  auto& sampleInstrument = project.instruments[1];
  getInstrumentFunctions(InstrumentType::Sample).init(&sampleInstrument);
  auto& sample = sampleInstrument.chip.sample;
  sample.frameCount = 4096;
  sample.sampleRate = 48000;
  sample.channels = 1;
  sample.data = static_cast<int16_t*>(std::malloc(sample.frameCount * sizeof(int16_t)));
  REQUIRE(sample.data);
  for (uint32_t i = 0; i < sample.frameCount; ++i)
    sample.data[i] = static_cast<int16_t>(std::sin(i * 0.071) * 24000.0);
  sample.loopMode = 1;
  sample.attack = sample.decay = 0;
  sample.sustain = 255;
  sample.release = 8;

  chipnomadInitChips(state, 48000, nullptr);
  auto* router = midiRouterCreate();
  REQUIRE(router);
  float audio[2000]{};

  // MIDI channel 1 is zero-based channel 0. The router converts MIDI note
  // 60 to the engine's C-0-relative note 48; velocity is preserved.
  enqueue(0x90, 0, 60, 100);
  REQUIRE(pollMidi(state, router, 1));
  REQUIRE(chipnomadRender(state, audio, 1000) == 1000);
  auto& track = state->playbackState.tracks[0];
  CHECK(track.midiHeld);
  CHECK(track.note.pitchBase == 48);
  CHECK(track.note.volume == 100);
  double energy = 0;
  for (float value : audio) energy += value * value;
  CHECK(energy > 1e-5);
  REQUIRE(state->sampleVoices[0][0]);
  CHECK(state->sampleVoices[0][0]->active());

  // Six 48 kHz / 48 Hz tracker ticks are one sixteenth note (24 PPQN).
  for (int tick = 0; tick < 5; ++tick)
    REQUIRE(chipnomadRender(state, audio, 1000) == 1000);
  enqueue(0x80, 0, 60, 0);
  REQUIRE(pollMidi(state, router, 1));
  REQUIRE(chipnomadRender(state, audio, 1000) == 1000);
  for (int tick = 0; tick < 24 && state->sampleVoices[0][0]->active(); ++tick)
    REQUIRE(chipnomadRender(state, audio, 1000) == 1000);
  CHECK_FALSE(track.midiHeld);
  CHECK_FALSE(state->sampleVoices[0][0]->active());

  // Repeated/overlapping notes use the router's last-note-priority stack:
  // releasing the older note does not stop the current one, releasing the
  // current note restores the older one, and its final release silences it.
  enqueue(0x90, 0, 60, 100);
  REQUIRE(pollMidi(state, router, 1));
  REQUIRE(chipnomadRender(state, audio, 1000) == 1000);
  CHECK(track.note.pitchBase == 48);
  enqueue(0x90, 0, 64, 90);
  REQUIRE(pollMidi(state, router, 1));
  REQUIRE(chipnomadRender(state, audio, 1000) == 1000);
  CHECK(track.note.pitchBase == 52);
  CHECK(track.note.volume == 90);

  enqueue(0x80, 0, 60, 0);
  CHECK_FALSE(pollMidi(state, router, 1));
  REQUIRE(chipnomadRender(state, audio, 1000) == 1000);
  CHECK(track.note.pitchBase == 52);
  CHECK(track.midiHeld);

  // Repeated note instances are paired in LIFO order and preserve the
  // restored note's original velocity.
  enqueue(0x90, 0, 60, 80);
  REQUIRE(pollMidi(state, router, 1));
  REQUIRE(chipnomadRender(state, audio, 1000) == 1000);
  CHECK(track.note.pitchBase == 48);
  CHECK(track.note.volume == 80);
  CHECK(track.midiHeld);

  enqueue(0x80, 0, 60, 0);
  REQUIRE(pollMidi(state, router, 1));
  REQUIRE(chipnomadRender(state, audio, 1000) == 1000);
  CHECK(track.note.pitchBase == 52);
  CHECK(track.note.volume == 90);
  CHECK(track.midiHeld);

  enqueue(0x80, 0, 64, 0);
  REQUIRE(pollMidi(state, router, 1));
  REQUIRE(chipnomadRender(state, audio, 1000) == 1000);
  CHECK_FALSE(track.midiHeld);
  for (int tick = 0; tick < 24 && state->sampleVoices[0][0]->active(); ++tick)
    CHECK(chipnomadRender(state, audio, 1000) == 1000);
  CHECK_FALSE(state->sampleVoices[0][0]->active());

  midiRouterDestroy(router);
  chipnomadDestroy(state);
  midiRouterSetBackend(nullptr);
}

TEST_CASE("Orca-c MIDI duration drives the existing sample voice on tracker ticks") {
  auto* state = chipnomadCreate();
  REQUIRE(state);
  auto& project = state->project;
  project.tickRate = 48.0f; // 24 tracker ticks per quarter note at 120 BPM.
  project.chipType = ChipType::AY;
  project.chipsCount = 1;
  project.tracksCount = projectGetTotalTracks(&project);
  calculatePitchTableAY(&project);

  auto& instrument = project.instruments[1];
  getInstrumentFunctions(InstrumentType::Sample).init(&instrument);
  auto& sample = instrument.chip.sample;
  sample.frameCount = 4096;
  sample.sampleRate = 48000;
  sample.channels = 1;
  sample.data = static_cast<int16_t*>(std::malloc(sample.frameCount * sizeof(int16_t)));
  REQUIRE(sample.data);
  for (uint32_t i = 0; i < sample.frameCount; ++i)
    sample.data[i] = static_cast<int16_t>(std::sin(i * 0.071) * 24000.0);
  sample.loopMode = 1;
  sample.attack = sample.decay = 0;
  sample.sustain = 255;
  sample.release = 8;
  chipnomadInitChips(state, 48000, nullptr);

  // Native Orca-c grid: '%' is banged once by '*'. Its parameters below are
  // channel 0, octave 5, C, velocity index 12 (95), duration 2 ORCA frames.
  constexpr Usz height = 2, width = 7;
  constexpr Usz eventCapacity = 4;
  std::vector<Glyph> grid = {
    '.', '%', '0', '5', 'C', 'c', '2',
    '.', '*', '.', '.', '.', '.', '.',
  };
  std::vector<Mark> marks(height * width, 0);
  std::vector<Oevent> events(eventCapacity);
  Oevent_list list{events.data(), 0, eventCapacity};
  const Oevent* const preallocatedBuffer = list.buffer;
  Usz noteOffFrame = SIZE_MAX;
  std::vector<float> audio(1000, 0.0f);
  double energy = 0.0;
  unsigned emitted = 0;
  constexpr unsigned trackerTicksPerOrcaFrame = 6; // 1/16 note at 24 PPQN.
  constexpr unsigned framesToRun = 6;

  for (unsigned trackerTick = 0; trackerTick < framesToRun * trackerTicksPerOrcaFrame; ++trackerTick) {
    if (trackerTick % trackerTicksPerOrcaFrame == 0) {
      const Usz frame = trackerTick / trackerTicksPerOrcaFrame;
      if (noteOffFrame == frame) {
        chipnomadDispatchMidiInputAudioOwned(state, 0, 0, 0, 0);
      }
      mbuffer_clear(marks.data(), height, width);
      oevent_list_clear(&list);
      orca_run(grid.data(), marks.data(), height, width, frame, &list, 0x1234);
      for (Usz i = 0; i < list.count; ++i) {
        CHECK(list.buffer[i].any.oevent_type == Oevent_type_midi_note);
        const auto& note = list.buffer[i].midi_note;
        CHECK(note.channel == 0);
        // MIDI note 60 is converted to the tracker engine's C-0-relative 48,
        // matching the normal MIDI input adapter's pitch convention.
        const unsigned midiPitch = 12u * note.octave + note.note;
        CHECK(midiPitch == 60);
        chipnomadDispatchMidiInputAudioOwned(state, 0,
          static_cast<uint8_t>(midiPitch - 12), 1, note.velocity);
        CHECK(state->playbackState.tracks[0].midiPending == 1);
        noteOffFrame = frame + note.duration;
        ++emitted;
      }
      CHECK(list.buffer == preallocatedBuffer);
      CHECK(list.capacity == eventCapacity);
    }
    CHECK(chipnomadRender(state, audio.data(), 1000) == 1000);
    for (float value : audio) energy += value * value;
  }

  CHECK(emitted == 1);
  CHECK(energy > 1e-5);
  CHECK(state->playbackState.tracks[0].note.volume == 95);
  CHECK_FALSE(state->playbackState.tracks[0].midiHeld);
  REQUIRE(state->sampleVoices[0][0]);
  CHECK_FALSE(state->sampleVoices[0][0]->active());
  chipnomadDestroy(state);
}

TEST_CASE("embedded ORCA score follows song transport tempo stop and restart") {
  auto* state = chipnomadCreate();
  REQUIRE(state);
  auto& project = state->project;
  project.tickRate = 48.0f;
  project.chipType = ChipType::AY;
  project.chipsCount = 1;
  project.tracksCount = projectGetTotalTracks(&project);
  calculatePitchTableAY(&project);
  project.song[0][0] = 0;
  project.chains[0].rows[0].phrase = 0;
  project.chains[0].rows[0].transpose = 0;
  phraseClear(&project.phrases[0]); // Keep the tracker transport active and silent.

  auto& instrument = project.instruments[1];
  getInstrumentFunctions(InstrumentType::Sample).init(&instrument);
  auto& sample = instrument.chip.sample;
  sample.frameCount = 4096;
  sample.sampleRate = 48000;
  sample.channels = 1;
  sample.data = static_cast<int16_t*>(std::malloc(sample.frameCount * sizeof(int16_t)));
  REQUIRE(sample.data);
  for (uint32_t i = 0; i < sample.frameCount; ++i)
    sample.data[i] = static_cast<int16_t>(std::sin(i * 0.071) * 24000.0);
  sample.loopMode = 1;
  sample.attack = sample.decay = 0;
  sample.sustain = 255;
  sample.release = 8;

  chipnomadInitChips(state, 48000, nullptr);
  REQUIRE(chipnomadQueueProjectRefresh(state));
  REQUIRE(chipnomadQueuePlaybackStartSong(state, 0, 0, 1));
  float output[2000]{};
  double energy = 0;
  auto renderOneThousand = [&]() {
    int rendered = chipnomadRender(state, output, 1000);
    REQUIRE(rendered == 1000);
    for (float value : output) energy += value * value;
  };

  // One queued song start is followed by the fixed ORCA score on master ticks.
  for (int i = 0; i < 3; ++i) renderOneThousand();
  REQUIRE(chipnomadGetPlaybackStatus(state)->transportPlaying);
  REQUIRE(state->orcaTransport.totalFramesExecuted >= 1);
  REQUIRE(state->orcaTransport.gateActive);
  CHECK(state->orcaTransport.events.buffer == state->orcaTransport.eventStorage);
  CHECK(state->orcaTransport.events.capacity == 4);
  CHECK(state->playbackState.tracks[0].note.pitchBase == 48);
  CHECK(state->playbackState.tracks[0].note.volume == 95);
  REQUIRE(state->sampleVoices[0][0]);
  CHECK(state->sampleVoices[0][0]->active());
  CHECK(energy > 1e-5);

  for (int i = 0; i < 6; ++i) renderOneThousand();
  CHECK(state->orcaTransport.gateActive); // One ORCA frame after note-on.
  for (int i = 0; i < 6; ++i) renderOneThousand();
  CHECK_FALSE(state->orcaTransport.gateActive); // Duration 2 releases at frame 2.
  CHECK_FALSE(state->playbackState.tracks[0].midiHeld);
  CHECK_FALSE(state->sampleVoices[0][0]->active());

  // Equal sample time produces twice as many ORCA frames when the master's
  // tick rate doubles from 48 to 96 ticks/second.
  const uint64_t before48 = state->orcaTransport.totalFramesExecuted;
  for (int i = 0; i < 6; ++i) renderOneThousand();
  const uint64_t steps48 = state->orcaTransport.totalFramesExecuted - before48;
  CHECK(steps48 == 1);
  project.tickRate = 96.0f;
  REQUIRE(chipnomadQueueProjectRefresh(state));
  const uint64_t before96 = state->orcaTransport.totalFramesExecuted;
  for (int i = 0; i < 6; ++i) renderOneThousand();
  const uint64_t steps96 = state->orcaTransport.totalFramesExecuted - before96;
  CHECK(steps96 == 2);

  // Stop releases the active ORCA gate, and a new transport start restores
  // the score's initial bang and runs its first frame deterministically.
  chipnomadQueuePlaybackStop(state);
  for (int i = 0; i < 3 && chipnomadGetPlaybackStatus(state)->transportPlaying; ++i)
    (void)chipnomadRender(state, output, 1000);
  CHECK_FALSE(chipnomadGetPlaybackStatus(state)->transportPlaying);
  CHECK_FALSE(state->orcaTransport.gateActive);
  CHECK_FALSE(state->playbackState.tracks[0].midiHeld);

  const uint64_t beforeRestart = state->orcaTransport.totalFramesExecuted;
  REQUIRE(chipnomadQueuePlaybackStartSong(state, 0, 0, 1));
  for (int i = 0; i < 3; ++i) renderOneThousand();
  CHECK(state->orcaTransport.totalFramesExecuted == beforeRestart + 1);
  CHECK(state->orcaTransport.frameNumber == 1);
  CHECK(state->orcaTransport.grid[8] == '.'); // The bang was restored, then consumed once.
  CHECK(state->orcaTransport.gateActive);
  CHECK(state->orcaTransport.events.buffer == state->orcaTransport.eventStorage);
  CHECK(state->orcaTransport.events.capacity == 4);

  chipnomadQueuePlaybackStop(state);
  for (int i = 0; i < 3 && chipnomadGetPlaybackStatus(state)->transportPlaying; ++i)
    (void)chipnomadRender(state, output, 1000);
  CHECK_FALSE(state->orcaTransport.gateActive);
  for (int i = 0; i < 4 && state->sampleVoices[0][0]->active(); ++i)
    (void)chipnomadRender(state, output, 1000);
  CHECK_FALSE(state->sampleVoices[0][0]->active());
  chipnomadDestroy(state);
}
