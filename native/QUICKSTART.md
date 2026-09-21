# Tide — Study 01

A playable instrument with folded, gently saturated voices and two spaces. Six patches explore short wooden plucks, glassy lines, bass and slow chords.

## Play

Open **Tide.app**. Click the on-screen keyboard to give it focus, then play **A W S E D F T G Y H U J K**, or click the notes. These computer keys start at MIDI note 48. You can also use a MIDI controller.

Use **Options → Audio/MIDI Settings…** to choose your output and enable a MIDI controller. Start at **48 kHz with a 256- or 512-sample buffer**. The instrument adds 192 samples of reported latency (4 ms at 48 kHz), in addition to the device buffer.

Start with **First light**, then try **Soft wood** and **Glass current**. Hold a chord with **Slow bloom**. **Low tide** suits bass; **Quiet wire** leaves room around a melody.

- **Timbre** changes the folding depth. Stronger notes open into richer harmonics.
- **Drive** rounds the waveform with soft saturation.
- **Motion** slowly changes harmonic colour. The mod wheel adds more movement.
- **Attack, Decay, Sustain, Release** shape the note. With Sustain at zero, a held key fades like a pluck.
- **Close** gives shorter, narrower reflections. **Bloom** builds into a wider tail. **Amount** controls how much space surrounds the voice.
- **Output** sets the final level. Dense chords and high space amounts receive gradual gain compensation.
- **Quiet** fades and clears voices and space. Pitch bend spans two semitones either way; sustain pedal is supported.

## Save sounds and use a DAW

In the standalone app, **Options → Save current state…** saves a patch file, and **Load a saved state…** recalls one. The last state is also restored after a normal quit. Factory preset changes replace the current settings; save a sound you want to keep first.

The **VST3** and **Audio Unit** are stereo MIDI instruments. Load Tide on an instrument track, enable MIDI monitoring, and play. Controls support host automation and settings are saved with the DAW project. The AU manufacturer is **Eric Ruud**. If a host was already running during installation, rescan its plugins or restart it.

## What this version is

This is an original instrument, developed from the liked wavefolder prototype and the Close/Bloom listening preferences. It uses 16x oversampling for the nonlinear voice, interpolated envelopes, eight voices and original synthetic stereo impulse responses.

It is an Apple Silicon macOS prototype, locally built and ad-hoc signed. It is not a calibrated Buchla circuit emulation. The album and hardware references guide the direction; none of their audio is embedded. Technical tests measure defined cases, while your listening decisions guide the sound.
