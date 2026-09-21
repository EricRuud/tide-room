# Tide Room 0.6.0 — Moving reflections

Open **Motion / LFOs…**. The menu at the upper right chooses the reflection sound:

- **Moving reflections** (default): Tide renders moving wall, floor and ceiling reflections. Reverside supplies the late reverberation at fixed source anchors. This supports continuous position LFOs without continuously rebuilding Reverside's geometry.
- **Reverside fixed**: retains Reverside's early-reflection character at each instrument's centre. In Headphones placement the direct sound can still move, but these reflections remain stationary. This is a comparison mode, not moving Reverside geometry.

The moving reflection sound differs from Reverside's original early reflections. Reverside's Shape, Fill, material and diffuser controls continue to affect its internal room and late-tail feed; they do not edit Tide's new wall model. **Shared Space** dimensions control the moving walls. **Reflections** adjusts room level, while **Tail** and **Decay** control Reverside's late field.

## Tape softness

The Spatial tape **HF softness** range now extends to **400%**. The old 100% setting keeps its previous strength; 200–300% is a useful place to explore a more exaggerated effect. This acts within the level-dependent nonlinear model. Spatial is a custom model, not a calibrated recreation of a named machine. See **SPATIAL-CONTROLS.md**.

## Making a path

1. Choose an instrument and axis, then enable an LFO. Start at **0.1 Hz / 25% depth**.
2. Choose **Sine** or **Triangle**. **Sync** changes the rate to a musical cycle length.
3. For a loop, use two sine LFOs on left/right and depth, with the same rate and a **90 degree** phase difference. Different rates make evolving paths.

The six LFOs, their destinations, rates, shapes, phases, depths and sync settings, and the reflection renderer are saved with the scene. Play restarts the phases; Stop holds them while tails decay; Quiet fades out. Multiple routes to one axis add together. The solid map dots show the result; hollow markers show the centres. Drag a marker to move its centre. The height sliders set centre heights.

Depth is the peak excursion as a percentage of half an axis range. At 100%, the excursions are ±0.8 in lateral map coordinates, ±0.29 of room depth, and ±0.95 m in height. Positions are clamped to the map. Reduce depth or move the centre away from an edge if a path lingers at the boundary. Route changes are smoothed over about 50 ms.

**Headphones** retains the measured KEMAR filters for direct sound. The moving reflections use ear distance, level and head-shadow approximations. **Stereo** uses speaker panning with the moving renderer; with Reverside fixed it retains the earlier Reverside placement. The Reflections level is available with either Headphones or Moving reflections.

## What changed

Reverside's bundled `AUTOMATION.TXT` warns that most parameters can produce artifacts when continuously automated. A live capture of 0.5.1 reproduced abrupt reflection-output discontinuities that vanished with LFOs disabled. A slower update rate did not solve this.

The replacement uses 24 persistent rectangular-room image paths per instrument: six first-order and eighteen second-order reflections. Delay, amplitude and two-stage high-frequency damping change continuously. Twelve-tap windowed-sinc fractional delays avoid integer-delay jumps. The existing smoothed propagation delay adds the common direct travel time. Fast or deep movement can produce Doppler pitch bends. An upper bound on reflection read-head speed prevents time reversal at extreme settings; this is a practical musical approximation, not an exact moving-wave solution or a claim of mathematically zero aliasing.

All three existing Reverside instances remain. Their early output gain is zero in Moving reflections mode; their internal early-to-late feed is retained. Switching to Reverside fixed restores the saved early output gain. Reverside geometry follows manually placed centres, never LFO offsets. Its room dimensions and manual geometry edits are still subject to the vendor's automation limits.

The new wall paths have fixed moderate absorption and progressively darker second-order reflections. This is a rectangular-room approximation with a generic listener, not a clone of Reverside's reflection algorithm. Existing synthesis and tape algorithms and quality controls remain available. The documented limitation in Reverside's advanced preset recall still applies; Tide saves its own controls independently.

## Verification and developer capture

Build with the existing JUCE 8.0.9 / Apple Silicon setup:

```sh
cmake --build build-room3 --target TideRoom_Standalone TideMotionCheck --parallel 3
build-room3/TideMotionCheck_artefacts/Release/TideMotionCheck --unit /tmp/new-motion-unit
build-room3/TideMotionCheck_artefacts/Release/TideMotionCheck --routing /tmp/new-motion-routing
build-room3/TideMotionCheck_artefacts/Release/TideMotionCheck --scene /tmp/new-motion-scene
```

The routing and scene checks require Reverside. Use fresh output directories. The tests cover continuous tone motion at 44.1/48/96 kHz, symmetry, static callback partition independence, tail drainage, state, routing, live parameter edits, deep/fast motion and both placement modes. These tests supplement the live audio capture; they do not establish subjective taste.

The standalone app accepts the optional developer argument `--capture-motion /absolute/output/directory`, with `--enable-motion` or `--disable-motion` for a comparison. This opens the vendor editor, starts the score, and records twelve seconds inside the actual audio callback. All capture memory is allocated before publication to the callback; files are written after capture completion. The 20-channel WAV contains the final mix, each synth input, each Reverside output and each moving-reflection output, with an accompanying channel map and position log. This path is inactive in ordinary launches. It records before the operating system and cannot detect downstream device dropouts.

See **TEST-REPORT.md** for measured results and limitations.

## Method reference

The rectangular-room image construction follows the established method described by [Allen and Berkley, *Image method for efficiently simulating small-room acoustics* (1979)](https://jontallen.ece.illinois.edu/uploads/537.F18/Papers/Public/Allen-PDFs/AllenBerkley79.pdf). The smoothing, binaural approximation, fixed path count and integration with Reverside are Tide implementation choices.
