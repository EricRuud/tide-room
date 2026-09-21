# Tide Room development

The supported standalone build is C++17/JUCE 8.0.9 with Metal shaders on Apple Silicon macOS 14+. From the repository root:

```sh
bash native/build-room.sh
```

`native/build-room.sh` checks the JUCE commit, configures the native Clear Room path, builds only `TideRoom_Standalone`, copies all runtime assets and ad-hoc signs the app. It does not launch the app or change user presets. `JUCE_PATH`, `CMAKE_COMMAND`, `TIDE_BUILD_DIRECTORY` and `BUILD_JOBS` may override local tool/build locations; JUCE must still match the pinned revision.

To configure manually after fetching JUCE, use:

```sh
cmake -S . -B build-room -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
  -DTIDE_NATIVE_WOOD=ON -DTIDE_CLEAR_ROOM=ON \
  -DTIDE_ROOM_OUTPUT_DIRECTORY="$PWD/build-room/app"
cmake --build build-room --target TideRoom_Standalone --parallel 3
```

A full Xcode installation with a working `xcrun -sdk macosx metal` is required to compile the shader library. Runtime playback does not need Xcode, CMake or an external plugin.

## Checks

Build the native checks, then use a fresh output directory for each run:

```sh
cmake --build build-room --target TideOceanCheck TideGpuCheck TidePresetCheck --parallel 3
build-room/TideOceanCheck_artefacts/Release/TideOceanCheck "$PWD/artifacts/check-plate-audio" --plate-audio
build-room/TideOceanCheck_artefacts/Release/TideOceanCheck "$PWD/artifacts/check-pitch" --note-lights
build-room/TideOceanCheck_artefacts/Release/TideOceanCheck "$PWD/artifacts/check-drag" --glass-controls
build-room/TideOceanCheck_artefacts/Release/TideOceanCheck "$PWD/artifacts/check-immersive" --immersive
build-room/TideGpuCheck_artefacts/Release/TideGpuCheck "$PWD/artifacts/check-gpu"
build-room/TidePresetCheck_artefacts/Release/TidePresetCheck "$PWD/artifacts/check-presets"
clang++ -std=c++17 -O2 native/Room/TowerMotionChecks.cpp -o build-room/tower-motion-check
build-room/tower-motion-check
```

GUI and Metal checks need a logged-in macOS desktop. They create their own test scenes and temporary preset libraries. The plate-audio check covers 44.1/48/96 kHz, separated synth stems and overlapping source heights. GPU checks compare the Metal renderer with the CPU reference and save screenshots. An optional saved scene can be measured with `TideOceanCheck NEW_OUTPUT --preset-audio /absolute/path/to/scene.tideroom`; it reads the preset without modifying it.

## Architecture

`RoomProcessor` schedules the score and motion on the audio clock. The shared `Engine` renders separate polyphonic stems; direct paths and early reflections use each pitch's plate elevation. The diffuse room, tape and master stages follow the source mix. Two persistent audio workers process independent instruments alongside the device callback.

`TowerNotes` defines pitch ordering and the shared visual/acoustic elevations. `TowerOptics` implements the glass and continuous water volume for both C++ and Metal. `RoomEffects.metal`, `GpuEffects.mm` and `RoomSceneRenderer` render the objects and case; visual effects do not alter audio. `RoomPresets` saves complete scenes while retaining older parameter IDs.

The `821` calibration files are tracked in `native/Assets/821`; their SHA-256 manifest is included. The tape interface exposes Worn tape and 821 while keeping the original five-engine parameter indices. `TapeModelSelector` maps the two visible choices explicitly to saved values 4 and 3; simplified scene recall preserves either choice. The app build does not depend on experiment output folders. Optional lab scripts can depend on measurement recordings and research artifacts that are not distributed here.

## Earlier instrument and laboratory workflow

The following notes describe the original Tide instrument and Tide FX targets, which remain available. Their historical render folders and listening feedback are local development data, not repository assets.

Build on Apple Silicon macOS with Xcode Command Line Tools:

Run the complete build/test/render iteration with `python3 native/evaluate.py`. It creates a timestamped folder, snapshots sources, stops on failed technical gates, tests the built VST3, and saves audio. The spectral ceiling and measurement band come from `criteria.json`. Individual stages are also available:

```sh
bash native/build.sh
build/TideCheck_artefacts/Release/TideCheck
build/TideCheck_artefacts/Release/TideCheck --probes artifacts/my-native-run/probes
python3 native/analyze.py artifacts/my-native-run/probes
build/TideCheck_artefacts/Release/TideCheck --render artifacts/my-native-run/presets
build/TideHostCheck_artefacts/Release/TideHostCheck "$PWD/build/Tide_artefacts/Release/VST3/Tide.vst3"
```

Use a new output folder per iteration. The source assets are included; `python3 native/generate_assets.py` regenerates the FIR, Bessel lookup and original stereo spaces using the pinned Python dependencies. The script fetches JUCE 8.0.9 if missing and verifies its commit. Generated assets contain no commercial reference audio.

`Engine.cpp` owns persistent voice, smoothing, decimation and convolution state. `Processor.cpp` handles sample-timed MIDI, host parameters, state and latency. `Editor.cpp` draws the native interface. `Checks.cpp` tests the engine and processor; `Host.cpp` loads the actual VST3 through a host boundary. The Python analysis builds an independent continuous-phase Fourier reference and omits out-of-band harmonics before synthesis.

The 16x voice feeds a 6,145-tap linear-phase Kaiser FIR with 192 host samples of latency. The fold is the original biased sine mapping with DC compensation. Drive uses a monotonic cubic on the folder's bounded output; it is a soft-saturation design, not a transistor circuit solution. A tested tanh alternative was rejected after high-register alias failures. Envelope and slow-motion functions are evaluated at twice the host sample rate and interpolated through the nonlinear path. Parameter values have one-pole smoothing. The fixed spaces use partitioned convolution and crossfade continuously.

Technical gates remain provisional: stationary relative residual at or below −90 dB, measured through 0.4 times the sample rate; no finite-value or clipping failures in the exercised MIDI/parameter stress cases. The residual includes quantization and filter error, not just aliasing. The current 168-case sweep covers 44.1/48 kHz, seven coherent pitches, four folding depths and three saturation amounts. Full-instrument modulation, arbitrary parameter automation and every possible MIDI history do not yet have a converged independent alias reference. Those are future evaluation work, not implied by the oscillator score.

The wider listening loop remains in `lab.py`, `space_round.py`, `criteria.json` and `feedback/`. Keep the liked baseline unchanged, make a new native iteration, rerun the affected checks, render comparable patches, and retain or reject changes based on both technical evidence and listening. No numerical metric substitutes for the user's taste or proves analog authenticity.

Runtime framework: JUCE 8.0.9, commit `f72bad64d29715216226685810c5196bd0d79d77`. Its licensing and third-party notices are in `.cache/JUCE/LICENSE.md` and the module source headers. Apple Accelerate supplies the FIR dot product on macOS.

## Standalone effect host

`TideFX_Standalone` builds the separate **Tide FX.app** with one stereo VST3 insert. `EffectHost.cpp` manages discovery, child-process scanning, plugin lifetime, its editor, state and latency-aligned bypass. `FXApplication.cpp` supplies the standalone application and scanner entry point. `TIDE_EFFECT_HOST` keeps these changes out of the original Tide AU/VST3. The voice engine and its spectral tests are unchanged.

With an installed effect, run the contained console checks using absolute paths and a new output directory:

```sh
build/TideFXCheck_artefacts/Release/TideFXCheck --effect-test /Library/Audio/Plug-Ins/VST3/DPA_Reverside.vst3 "$PWD/artifacts/my-effect-run"
```

This exercises the same processor and effect host as the app without opening an audio device. It checks delayed bypass, empty-slot sound against the original engine, child-process scanning and failure recovery, then renders the installed effect at 44.1, 48 and 96 kHz. Exit zero means the audio/host checks passed. Inspect `allChecksPassed` and the separate state fields in `report.json` for complete validation: Reverside state recall currently fails even in full mode. The test also compares encoded state bytes and a direct JUCE restore. This does not replace the live editor/device test. See [the FX guide](FX-QUICKSTART.md) for operation and current scope.

## Percussion voices in 0.3

The original Flow branch remains available. Pluck, Knock and Metal add a two-pole low-pass response driven by fast/slow light-memory envelopes; frequency and amplitude fall together. Knock has a short upward pitch transient. Metal mixes the original folder with a 7:5 phase-modulated sine before the bounded cubic. These are perceptual designs, not component-level Buchla models. A struck note captures its response and zero-sustain behavior at note-on, so later control edits cannot latch an already released percussion note.

```sh
build/TidePluckCheck_artefacts/Release/TidePluckCheck --checks artifacts/my-plucks
build/TidePluckCheck_artefacts/Release/TidePluckCheck --probes artifacts/my-pluck-probes
python3 native/analyze_percussion.py artifacts/my-pluck-probes
build/TidePluckCheck_artefacts/Release/TidePluckCheck --benchmark artifacts/my-pluck-benchmark
```

The new spectral reference constructs the stationary metallic waveform independently in continuous phase, retains only frequencies below host Nyquist, and compares at the existing fixed 192-frame latency through 0.4 times the sample rate. Its 168 cases cover 44.1/48 kHz, seven carrier frequencies, three modulation indices, two folding amounts and two saturation amounts. This does not measure aliasing from arbitrary moving-gate/pitch/envelope trajectories. Processor tests cover the expanded preset bank and missing-parameter migration; percussion checks cover trigger length, block boundaries, velocity, stopping, sustain edits and 36 eight-voice extremes. `--ui` renders the editor offscreen for layout inspection; it does not operate the user's desktop session.
