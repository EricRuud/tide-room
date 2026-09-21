# Studio 80 — 15 ips

Tide Room 0.7.0 adds **Studio 80 / 15 ips** alongside the existing Magnetic and Spatial engines. It is an A80-inspired studio-tape model, with its reference recording curve fitted to published SM911 measurements. It is not a verified clone of an individual Studer A80.

Open **Tape controls…**, select **Studio 80 / 15 ips**, and turn **Tape on**. Your room is set to **Cream**, **16× Finest**, and 100% Mix.

| Control | What it does |
|---|---|
| Recording drive | Raises the field into the recording model, with inverse input-gain compensation. Saturated peaks become smaller relative to the body. |
| HF cream | 0% selects the fitted reference curve. Increasing it extends the short-wavelength overload behavior and suppresses generated upper harmonics. It is not a static output tone control. |
| Bias colour | Relative movement around the reference curve. This is a creative bias surrogate, not a measurement of bias current or overbias. |
| Transport | Shared stereo wow, flutter, and slower irregular drift. 100% is restrained; 300% exaggerates it. Zero is perfectly steady. |
| Tape texture | Hiss and signal-dependent grain. 100% has approximately the SM911 reference A-weighted noise level at zero drive. Zero removes both. |
| Output trim | A fixed gain adjustment. No compressor or automatic loudness matching. |
| Mix | Blends against the latency-aligned dry room. At partial Mix, transport motion can also produce comb filtering; 100% is recommended for the intended machine effect. |
| Oversampling | 4×, 8×, or 16×. Use 16× for heavy drive; 8× saves CPU. Switching fades through dry while the new path fills. |

**Reference** uses the fitted curve, no extra recording drive, a steady studio transport, and nominal tape texture. It is deliberately subtle on quiet inputs.

**Cream** uses +18 dB recording drive, 100% HF cream, restrained movement, 15% texture, and +0.5 dB output trim. This is the starting sound for your current room.

**Bloom** uses +27 dB drive, 220% HF cream, a little bias colour, 180% transport, 25% texture, and +3.5 dB trim. Expect a much denser body and substantially smaller peaks.

Nominal tape latency remains 768 samples (16 ms at 48 kHz), including bypass. Wow/flutter adds tiny time-varying deviations around that nominal delay. Stop allows the musical room tails to decay, fades the remaining tape hiss, and lets the app sleep. Play resumes smoothly.

The measured reference and creative controls are intentionally distinct. The model includes multiple recording-depth states, head resonance, low-frequency coupling, finite replay bandwidth, shared transport motion, hiss, and modulation noise. It does not yet reconstruct a particular A80's electronics, physical AC-bias waveform, full hysteresis minor loops, azimuth errors, print-through, worn heads, or dropouts.

See the accompanying **TAPE-STUDY.md** for measurements, comparisons with hardware, antialiasing limits, and reproducibility information.
