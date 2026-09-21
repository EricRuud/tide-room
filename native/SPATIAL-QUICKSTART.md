# Tide Room 0.4.1 — Spatial saturation

Open **Tide Room.app**, use headphones, and press **Play**. Start with **48 kHz / 1024 samples** in Options → Audio Settings.

Open **Tape controls…** and choose **Spatial** under Model. Use the **Tape on / Tape off** button in either window. Fresh scenes start with Spatial selected; scenes saved in earlier versions retain the original magnetic model and their previous Tape on/off setting.

The starting sound is **Drive 27.6 dB, HF softness 25%, Output trim +6.8 dB, Mix 100%, 4× Reference**. Drive and softness reproduce the research prototype's operating point; trim is a fixed starting balance, not automatic loudness matching. The offline auditions were individually gain matched, so different scenes may benefit from a trim adjustment.

- **Drive** pushes the combined instruments and reverb into the saturation. Try 30–33 dB for more density; 36 dB is the maximum.
- **HF softness** ranges from **0–400%** in 0.6.0. **100% is the former maximum**; saved amounts retain their previous strength, and 25% remains the research reference. Try 200–300% for an exaggerated effect. The control increases the level-dependent restraint of high harmonics inside the nonlinear model, including harmonics generated from lower notes. It does not add a conventional compressor or a static post-effect low-pass filter. A fourfold coefficient range does not mean four times the attenuation.
- **Output trim** sets the saturated level. Lower it when comparing against bypass so a louder result does not dominate the comparison.
- **Mix** blends with the latency-aligned original room.
- **Oversampling** trades processing cost against nonlinear aliasing. **4× Reference** is the recommended starting point. **2× Balanced** saves CPU and was extremely close on the tested music, but high-frequency stress tones reveal a difference. **1× Eco** saves more and can introduce substantial aliasing with bright, strongly driven material.

The live **DSP load** display measures the fraction of the audio callback's time budget used by the complete scene. It is smoothed, so a low average does not rule out a brief overload. **Peak DSP** and **Over-budget blocks** show the worst callback and missed callback budgets since the audio setup was prepared. They reset when the audio setup is reinitialized. If playback crackles, try 2× or a larger device buffer. This control affects Spatial saturation; the synth engines and Reverside keep their own processing quality.

All saturation modes and bypass have a fixed **768-sample delay**: 16 ms at 48 kHz, 17.4 ms at 44.1 kHz, and 8 ms at 96 kHz. The synth adds another 192 samples; audio-device buffering and any hosted-plugin delay are additional. Changing oversampling fades briefly through dry without changing latency. The first transient after a fresh preparation is fully processed.

**Magnetic (0.3)** retains the earlier CHOW-derived magnetic engine and its Drive, Saturation, Bias, Warmth and Mix controls. Its 32× oversampling is fixed. It is padded to the same 768-sample delay in this app. Select it to return to the original sound. Your existing instruments, room geometry, generative patterns and Reverside controls carry over.

The Spatial model is an original experimental saturation design based on the research work, rather than a calibrated emulation of a named tape machine. Its 4× setting prevents polynomial-product folding in each fixed Fourier window. Finite windowing, the bounded numerical solve, changing controls, and the rest of the audio chain remain separate sources of error; “Reference” is not a claim of literal zero aliasing for the entire app.

The engine now skips six inaudible built-in reverbs, reuses solver work, and sleeps after stopped tails fall below −160 dBFS for a full second. Audible quality settings and latency are retained.

See **TEST-REPORT.md** for measured CPU cost and waveform comparisons. **ROOM-CONTROLS-0.3.md** covers the retained room and instrument controls, including the advanced Reverside recall limitation; its tape timing and first-launch defaults describe the old release. The two research documents explain the model and tape physics. Reverside itself is loaded from your installation and is not bundled.

## Build and verify

Requires macOS on Apple Silicon, Xcode command-line tools, CMake 3.22+, and JUCE 8.0.9 at commit `f72bad64d29715216226685810c5196bd0d79d77`. The FFT implementation uses Apple's Accelerate framework. `native/build.sh` obtains/checks the pinned JUCE source. To build only this app after JUCE is available:

```sh
cmake -S . -B build-room3 -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-room3 --target TideRoom_Standalone TideSpatialCheck TideEfficiencyCheck --parallel 6
build-room3/TideSpatialCheck_artefacts/Release/TideSpatialCheck --checks /tmp/new-spatial-checks
```

The checks exercise sample rates, callback partitioning, worker determinism, stereo isolation, bypass, silence, strong drive, parameter transitions and state migration. `--probes`, `--render OUTPUT INPUT.wav`, `--scene OUTPUT` and `--scene-reference OUTPUT` produce additional verification audio; each output directory must be new. Scene tests require the installed Reverside VST3.

## Machine identity

Spatial is a custom research-derived saturation model. It is not calibrated to a named tape machine or tape formulation. It emphasizes smooth nonlinear density and level-dependent high-frequency restraint; naming an ATR, Studer or other specific machine as its closest match would require matched measurements and listening comparisons that have not been performed.
