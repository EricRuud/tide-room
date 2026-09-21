# Tide Room

A generative spatial synthesizer for Apple Silicon Macs. Three instruments move through a glass room, with water driven by their motion, gravity and collisions.

![Tide Room](docs/images/room.png)

Each instrument is a stack of glass plates. A plate represents a pitch, lights when that note plays, and places its sound at the same height in the room. Overlapping notes keep their own spatial positions.

- Wavefolding, low-pass-gate, ring-modulated, AM and metallic voices.
- Eight patterns, key and chord selection, density and evolving accents.
- Tides that move the instruments and modulate their sound; full routing in **Advanced…**.
- Measured headphone spatialization, moving wall reflections and a shared room tail.
- Metal-rendered water and glass, note-reactive prism effects, and fullscreen listener view.
- Worn tape, a master fader, complete-scene presets and stereo WAV recording.

The current version is **0.12.0**. This is an experimental instrument; the current interface exposes Worn tape while earlier tape models remain in the source for preset compatibility.

## Build and run

Requirements: an Apple Silicon Mac running macOS 14 or later, Xcode with its command-line tools and Metal compiler, CMake 3.22 or newer, and Git. The build fetches the pinned JUCE 8.0.9 source on first use. Python is not required to build the app.

```sh
git clone https://github.com/EricRuud/tide-room.git
cd tide-room
bash native/build-room.sh
open "build-room/app/Tide Room.app"
```

The app includes its runtime assets and native room processing; no external audio plugin is required. Local builds are ad-hoc signed. Developer ID signing and notarization are separate from this source build.

Start with headphones, 48 kHz and a 1024-sample buffer. Choose the output device in **Options** and press **Play**. See the [playing guide](native/ROOM-QUICKSTART.md) and [development guide](native/DEVELOPMENT.md) for controls, architecture and checks.

## Inside the room

Drag a tower left/right and nearer/farther across the floor plane. Each pitch occupies a fixed plate in its stack. Water surrounds the whole stack and responds to the other instruments. **Fullscreen immersive** puts the camera at the listener; **Esc** returns to the controls.

![Listener view](docs/images/immersive.png)

## Presets and recordings

**Slow tides** is included. **Save as…** stores the whole scene, and the preset selector can recall or import scenes. On macOS, user presets live in `~/Library/Tide Room/Presets` and recordings in `~/Music/Tide Room Recordings`. The app also remembers its last session and audio-device settings.

**Record** captures the final stereo mix to a 32-bit float WAV. To keep a room tail, stop playback, let the sound fade, then stop recording.

## Source layout

- `native/Room`: scene, audio placement, physics, Metal rendering, controls and checks.
- `native/Source`: shared synth engine and earlier instrument/plugin editions.
- `native/Assets` and `native/generated`: icons, factory scene, filters, original impulse responses and calibration data needed to build.
- `native/Lab`: retained DSP experiments and model-generation tools. Some research scripts expect local measurement data that is not part of this repository.
- Root Python files and `tests`: the earlier offline synthesis and listening laboratory.

## License and credits

**Reverb DSP:** Thanks to **Chris Johnson / [Airwindows](https://github.com/airwindows/airwindows)** for **kWoodRoom**, the reverb DSP used by Tide Room's retained WoodRoom engine. The upstream source, MIT license and copyright notice are included. See the [reverb attribution](native/Assets/ThirdPartyNotices/Airwindows-kWoodRoom-NOTICE.txt).

Original Tide Room code is copyright © 2026 Eric Ruud and licensed under **AGPL-3.0-only**, except where a file states other terms. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the full terms and component-specific licenses. JUCE, the CHOW Tape adaptation, Airwindows DSP and MIT KEMAR measurements retain their respective notices.
