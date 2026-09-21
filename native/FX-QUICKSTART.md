# Tide FX 0.3 — plucks and percussion

Tide with fourteen presets, four voice responses, and one stereo VST3 effect slot.

## Start with the new sounds

Choose **LPG ping**, **Bongo sun**, or **Copper tine** from the preset dropdown. The eight new sounds are below the separator after the original six. Play short notes and vary velocity; start in the lower middle of the keyboard.

| Preset | Character |
|---|---|
| LPG ping | Bright opening, rounded wooden decay |
| Bongo sun | Hollow, springy percussion with a short pitch drop |
| Dry twig | Tight ticks for fast patterns |
| Rubber pebble | Soft, low knocks |
| Copper tine | Metallic attack that settles into a singing tone |
| Small bell | Brighter ringing with a longer tail |
| Seed sequence | Short, touch-sensitive plucks |
| Hollow marimba | Round wood with a longer decay |

The **Voice** dropdown offers **Flow** (the original voice), **Pluck**, **Knock**, and **Metal**. It applies to new notes. The percussion responses darken as they decay; Knock adds a short pitch drop and Metal adds a second oscillator at a 7:5 ratio.

With **Sustain at zero when you strike a note**, the new responses ring out even after a quick key tap. **Decay** controls their length. Raise Sustain before playing to use held notes and the Release control. **Timbre** changes the opening brightness and metallic complexity, **Drive** adds soft saturation, and **Motion** adds slow colour changes and adjusts how the gate closes. Harder strikes open into brighter, stronger notes.

Click the on-screen keyboard and play **A W S E D F T G Y H U J K**, or enable your MIDI controller through **Options → Audio/MIDI Settings…**. Start at 48 kHz with a 256- or 512-sample buffer.

These are original West Coast-inspired responses. The moving filter is a perceptual low-pass-gate design, not a calibrated vactrol or Buchla circuit emulation. Buchla's [Easel description](https://buchla.com/product/music-easel-modern/) and [manual](https://buchla.com/guides/Buchla_Music_Easel_Manual.pdf) informed the focus on coupled tone/volume and oscillator modulation.

## Use Reverside

1. Open **Tide FX.app**.
2. In **External effect**, choose **DPA_Reverside**.
3. Click **Open editor** to use Reverside's own controls, presets and activation screen.
4. Play Tide using your MIDI controller or the on-screen keyboard. Set the balance between dry synth and reverb with Reverside's own wet/dry control.

**Bypass** smoothly compares the processed sound with the dry signal and compensates for the effect's reported latency. The effect keeps running during bypass. **Tide space** includes Close/Bloom before the external effect; loading a new effect switches it off initially. **Output** controls the final level after the external effect. **Quiet** fades and silences the output until the next note, clears the synth and requests an effect reset. Reverside retains its tail on reset, so remaining ambience can return with the next note.

Choose **No external effect** to remove the insert. Use **Refresh** after installing another VST3. The list reads the standard user and system VST3 folders; the selected bundle is checked for stereo effect compatibility when loaded. Instruments and effects requiring other channel layouts are not supported by this first version.

## Save and recall

Use **Options → Save current state…** to save the synth, selected effect and its standard plugin state together. **Load a saved state…** restores the synth and requests the effect's saved state. The session is also saved on a normal quit. An installed copy of that effect must remain available.

**Reverside settings recall is unresolved in this preview.** Reverside 1.0.1 did not restore a changed wet/dry control in the console tests, including after activation and into a fresh instance. A direct JUCE state restore showed the same result; Tide preserved the plugin's state bytes correctly. Save important Reverside sounds using its own preset controls, and verify recall yourself before relying on a Tide session file. A live editor test is still pending.

The [vendor manual](https://devproaudio.com/pub/reverside_manual.pdf), section 2.3, also lists project-state recall and preset saving as disabled in demo mode. That explains the earlier demo restriction, but does not establish the cause of the later full-mode test failure.

Tide FX uses its own settings file and copies the earlier Tide sound and audio settings on first launch. Factory synth presets change the synth controls while retaining the external effect. The first six presets keep their previous numbering and sound; earlier session files select Flow automatically. The installed original Tide AU/VST3 remain the earlier instrument edition. To use those in a DAW, load Tide and put Reverside on the following effect insert.

## Build details

Apple Silicon macOS 12 or later. Locally built and ad-hoc signed. Reverside is loaded from your installation; it is not included in this package. Scanning runs in a separate process, while the loaded effect runs inside Tide FX. Effect crashes can therefore close the app. The host does not change the effect's oversampling or guarantee its alias performance. Tempo-synced effects currently receive a fixed 120 BPM clock.
