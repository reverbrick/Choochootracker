# Launchpad Gate ENV hardware test

This is a temporary test build for the Launchpad Mini MK3 performance pad on
the TrimUI Brick. It uses the existing Launchpad connection and audio command
queue. The first build enables only Mixer Performance Mode's lower-left pad
(track 1, FX row 1 / Gate 1).

## Configuration

On an instrument assigned to track 1, set:

1. Mod 1 source to **GATE ENV**, Gate 1, and choose **Filter Cutoff** as its
   destination with a positive amount.
2. Mod 2 source to **GATE ENV**, Gate 1, and choose **Delay Send** as its
   destination with a positive amount.
3. Give the two slots different Attack and Release values so their independent
   envelope settings can be distinguished. Sustain and Decay are also per
   slot. GATE ENV durations use the tracker's existing ADSR tick units.
4. Use an instrument with a filter and an active delay effect. Keep a note
   sounding on track 1 while testing the filter; filter modulation changes
   sounding instrument voices. Delay send modulation is read from the active
   track instrument's modulation state.

## Hardware checks

1. Confirm Launchpad integration is enabled in the existing setting and open
   the main Mixer page. The lower-left pad should be blue while idle.
2. Hold the lower-left pad. Its LED should turn green. Hear/observe both the
   filter cutoff and delay send change with their separately configured
   attacks and sustain.
3. Release during the attack and listen for a smooth return using each slot's
   independent release time. Repeat after reaching sustain.
4. While holding the pad, play another note and change chain/pattern. The gate
   envelope should continue rather than restart from MIDI note events or the
   chain change.
5. Switch away from the Mixer Performance page, stop transport, and disconnect
   the Launchpad in separate trials. Each must release the held envelope.
6. Confirm existing ADSR/LFO modulation and ordinary MIDI Learn still work.

The software test checks gate/ADSR progression and that two slots on the same
gate receive track-scoped events. It cannot verify audible cutoff/delay
behaviour, disconnect recovery, or absence of audio artifacts; those require
the Brick and Launchpad.

Gate state belongs to the track. Each instrument modulation slot owns its own
envelope state. Voices rendered by one track share that track's slot output;
each active Layer child instrument keeps its own modulation slots while
receiving the parent track's gate events.

## Separate Pak installation

Build the `feature/external-gate-envelopes` ref with the repository's
`PortMaster` GitHub Actions workflow. That workflow also emits the NextUI Pak
artifact using the existing NextUI packaging target. Extract only
`choochootracker.aarch64` from that artifact for this temporary side-by-side
test Pak. Keep the standard Pak Store installation and its `launch.sh`,
`pak.json`, data, and settings unchanged.

Use a distinct directory such as
`/mnt/SDCARD/Tools/tg5040/ChooChooTracker-GateEnv-Test.pak`, with a test
`pak.json`, launcher, executable, and log. The launcher should use the original
Pak directory as its working directory for shared assets, while running the
test executable from the side-by-side directory so settings/autosave remain
separate.

Before copying, check the target directory does not exist and save the
executable SHA-256 locally. To roll back while preserving logs and settings,
rename the test directory so Pak Store no longer discovers it:

```sh
adb shell 'mv /mnt/SDCARD/Tools/tg5040/ChooChooTracker-GateEnv-Test.pak /mnt/SDCARD/Tools/tg5040/ChooChooTracker-GateEnv-Test.pak.disabled'
```

Delete the `.disabled` directory only after copying any wanted logs/settings
out. No command in these instructions replaces the original Pak Store
executable.
