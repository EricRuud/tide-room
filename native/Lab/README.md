# Airwindows tape listening experiment

This target compares Tide Room 0.3, a pinned unmodified ToTape9 DSP reference, and an experimental hybrid. It is an offline lab, not part of the released app. The app's tape source and existing release remain unchanged.

Build `TideTapeLab` through the root CMake project with the pinned JUCE 8.0.9 checkout. Example commands from the project root:

```sh
.cache/build-tools/cmake/data/bin/cmake --build build-room3 --target TideTapeLab --parallel 6
build-room3/TideTapeLab_artefacts/Release/TideTapeLab --checks
build-room3/TideTapeLab_artefacts/Release/TideTapeLab --render input.wav new-output-directory
build-room3/TideTapeLab_artefacts/Release/TideTapeLab --probes new-probe-directory
python3 native/Lab/analyze.py new-probe-directory
build-room3/TideTapeLab_artefacts/Release/TideTapeLab --bench input.wav new-timing-directory
```

`--hybrid-render` and `--hybrid-probes` rerender only the changing experimental model. The analysis script accepts an optional second directory containing the unchanged Tide and Airwindows reference probes. All output directories must be new. `listening.py` accepts an experiment directory with `room/` and `plucks/` renders and requires ffmpeg, NumPy and SoundFile. It measures loudness and applies one fixed gain per version; no dynamics processing is added for matching.

The reference calls the upstream ToTape9 double-precision processing function at the file's native sample rate. The C random generator is seeded identically before each instance. ToTape9 defaults are 0.5 for all nine controls; Flutter is set to 0, Input is set to give the stated 6 or 12 dB boost, and a fixed reciprocal gain is applied after processing. The head bump remains at its default for listening and is disabled for the stationary spectral and dynamic-response tests. Other controls remain at their defaults. Output's built-in clipper remains part of ToTape9. A one-sample delay was checked with a low-level impulse at 48 kHz; flutter is disabled for latency alignment. This is not the user's older installed Airwindows Consolidated build.

The hybrid uses the same 32x magnetic path, gain compensation, emphasis and treble filter as Tide Room 0.3. A new pre-magnetism brightness stage blends toward a 4.5 kHz one-pole filtered signal. Its detector measures departure from filtered history, averages squared departures across the ears, smooths energy with two 0.5 ms poles and maps it through a bounded rational curve. The shared amount controls both channels, while their audio and magnetic states remain separate. This is an original design inspired by the intention of TapeHack2, not a port of its averaging filters or nonlinear equations. At zero added amount the measured output matches the current tape exactly.

The first instantaneous-detector hybrid failed the −90 dB stationary criterion. The smoothed revision passed the sampled spectral and stability checks. This is not a universal aliasing bound, physical calibration or a substitute for listening. Full results, source hashes and comparison files are in `artifacts/room-004`.

The Airwindows source and MIT license are in `airwindows/`. The hybrid is a GPL-3.0 experimental fork of Tide's CHOW-derived tape stage; see `../third_party/chowtape` for its original license and attribution. No Airwindows code is added to the released Tide Room application by this experiment.

## Cream saturation voicings

`CreamTape.h` wraps the existing hybrid for fixed-setting offline auditions. A
non-resonant shelf attenuates treble before the driven tape; two non-resonant
one-poles roll off the output afterward. This adds no separate envelope
compressor, limiter, automatic gain, dry blend or modulation. The hybrid's
existing signal-dependent brightness control remains active. Additional input
gain is compensated by a fixed reciprocal trim before the final output filter.

Three voicings are included: Smooth (+30 dB drive, 3 dB input treble attenuation,
5.2 kHz output poles), Cream (+30 dB, 5 dB, 3.5 kHz), and Dense (+33 dB, 7 dB,
2.5 kHz, with a lower magnetic saturation point). The two output poles together
are approximately -6 dB at their stated frequency; they are not a resonant or
Butterworth filter. Settings are selected at prepare time, not live automated.

These auditions use 64x tape oversampling. The initial 32x version's relative
stationary error criterion failed on a heavily attenuated 17.6 kHz stress tone;
both iterations and measurements are retained under `artifacts/room-006-cream`.
64x is an offline quality choice with a higher CPU cost, not a real-time release
optimization. The released app and its current tape settings are unchanged.

```sh
build-room3/TideTapeLab_artefacts/Release/TideTapeLab --cream-checks
build-room3/TideTapeLab_artefacts/Release/TideTapeLab --cream-render input.wav new-output-directory
build-room3/TideTapeLab_artefacts/Release/TideTapeLab --cream-probes new-probe-directory
```

The cream renderer rejects non-finite output, numerical recovery, and any
excursion to the magnetic model's safety clamp. The input sources used in the
auditions peak below 0.21; the fixed extreme-drive presets do not claim arbitrary
full-scale input headroom. Stability checks cover 44.1, 48 and 96 kHz, block
partitioning, mono centering, channel isolation, and burst decay. The spectral
check measures stationary non-harmonic energy through 0.4 times the sample rate;
it does not establish zero aliasing for every possible signal.

## Retuning the existing hybrid's dynamic behavior

`BrightnessSettings` exposes the existing cutoff, threshold and detector time
as constants selected before `HybridTape::prepare`. Defaults preserve the old
equations and values. The new dynamic auditions change only cutoff and threshold:
Original 4500 Hz / 0.12, Responsive 1800 Hz / 0.30, Deep 1200 Hz / 0.45. All keep
the two 0.5 ms detector poles, +30 dB total drive, the same tape parameters,
full softening amount, and 64x oversampling. The extra Cream input/output filters
are absent; the original hybrid's own fixed emphasis and output tone remain.

Lowering the softening cutoff increases the attenuation available at high
activity; raising the threshold preserves more quiet treble. Keeping cutoff
times threshold at 540 approximately preserves the low-frequency detector
sensitivity. This is useful over a finite operating range; the blend still tends
toward a fixed low-pass at extreme activity and is not an absolute HF limiter.

Use `--dynamic-render INPUT NEW_OUTPUT_DIR`, `--dynamic-probes NEW_PROBE_DIR`,
and `--dynamic-checks`. Results and playback files are in
`artifacts/room-007-dynamic`. The experiment's analyzer measures input/output
curves across six levels as well as stationary non-harmonic energy. The defaults
also reproduce the first 1.5 seconds of all three previous Cream renders exactly.
