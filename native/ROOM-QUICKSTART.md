# Playing Tide Room

Build with `bash native/build-room.sh`, then open `build-room/app/Tide Room.app`. Choose your audio device in **Options**. Start with headphones at 48 kHz and a 1024-sample buffer, then press **Play**.

## Notes and tones

The top panel holds tempo, Evolution, key, scale, chord, pattern and three instrument controls. Each instrument has a sound, Level, Brightness, Note decay, Density and Mute. Choosing a sound retains its rhythmic role and position.

**A–G** select keys; **Shift + letter** selects a sharp. **1–7** select chords. **Q W R T Y U I O** select the eight patterns. Existing notes finish naturally when harmony changes. **New variation** chooses repeatable accents. **Stop** ends the score and lets sound decay; **Play** restarts it; **Quiet** fades the output to silence.

## Glass towers and water

A tower has one plate per distinct pitch in its pattern. Lower notes have larger, lower plates. Only the played plate lights. Each sounding note has its own spatial source at that plate's elevation, including overlapping tails.

Unlit plates are transparent dark glass inside one water envelope. The stack centers sit at the lower golden section, about 38.2% of room height. The listener sits 1.10 m above their centers. Very shallow rooms slightly compress stack height to retain floor clearance.

Drag left/right and nearer/farther across the floor plane. The wheel changes depth. **Fullscreen immersive** opens the listener's view; press **Esc** or **Exit fullscreen** to return.

## Tides and space

The left panel controls current speed, travel, gravity and sound coupling. **Modulate sound** enables the water's effect on timbre. **Advanced…** contains the individual motion sources, physics, masses, ocean routing and positions. Collisions produce a bounce and a patchable impact signal.

The right panel controls room width/depth, decay, tail, reflections and damping. **Headphones** uses measured ear filters; **Stereo** uses speaker panning. Both include propagation, moving wall reflections and a shared diffuse tail.

Height controls and vertical motion destinations are hidden. Older height values remain serialized for preset compatibility, but do not move the tower centers.

## Tape, master and recording

The bottom panel contains the tape model selector and the final master fader. Choose **Worn tape** or **821** while playing. Each keeps its own controls; **Enabled** and **Mix** are shared, and the complete selection is saved with the scene. The arrows show the audio path through the interface.

**Worn tape** exposes medium, presets, drive, age, transport, contact loss, dip depth, hiss, output and mix.

**821** offers **456 / 15 ips** and **900 / 30 ips**, drive, output, wow, flutter and a transport switch. Start at 0 dB drive; increasing drive is output-compensated. **Native**, **4x limiter** and **8x limiter** select the final limiter's quality. The models require **48 kHz**: select it in **Options** if the tape panel displays a sample-rate message. At other rates 821 passes aligned dry audio. Switching the model, calibration or quality briefly fades through dry at the same nominal latency.

**Record** writes the final stereo mix to a 32-bit float WAV in `~/Music/Tide Room Recordings`. To capture a tail, stop playback first and stop recording after the sound fades. **Show file** reveals the finished take.

## Presets

The selector recalls complete scenes, including sound, room, tides, tape and visuals. **Save as…** creates a new named scene; it does not overwrite existing presets. Import and the preset folder are available from the selector. Files live in `~/Library/Tide Room/Presets` on macOS.

**Slow tides** is the included scene. Earlier presets remain compatible, including parameters that are no longer exposed in the simplified interface. Audio-device settings are stored separately from scene presets.
