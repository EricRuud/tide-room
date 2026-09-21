"""Original offline spatial sketches using fixed, synthetic stereo impulse responses.

Early reflections plus frequency-dependent decaying colored noise. This is an
artistic FIR design, not a measured room, analog model, or recreation of Etch.
Noise generates the fixed filter, not an additive audio source. Processing is
linear and time invariant: no pitch shifting, nonlinear drive, or moving delay.
Background: Julius O. Smith, Physical Audio Signal Processing,
https://www.dsprelated.com/freebooks/pasp/Late_Reverberation_Approximations.html
"""
from dataclasses import asdict, dataclass

import numpy as np
from scipy import signal


@dataclass(frozen=True)
class Space:
    name: str
    predelay: float
    buildup: float
    low_t60: float
    mid_t60: float
    high_t60: float
    width: float
    early_fraction: float
    wet_db: float

    def settings(self):
        return asdict(self)


PRESETS = (
    Space("close", .009, .020, .65, .42, .20, .35, .65, -9),
    Space("bloom", .028, .100, 2.2, 1.5, .65, .75, .24, -5),
    Space("suspended", .048, .200, 3.8, 2.6, 1.0, .95, .12, -1),
)


def _unit_energy(x):
    energy = np.sqrt(np.mean(np.sum(x * x, axis=0)))
    if not np.isfinite(energy) or energy < 1e-15:
        raise ValueError("Impulse response must have finite, nonzero energy")
    return x / energy


def impulse_response(sr, preset, seed=208):
    if sr not in (44100, 48000):
        raise ValueError("This sketch supports 44.1 and 48 kHz")
    if not (0 < preset.buildup < 1 and 0 <= preset.predelay < .5
            and 0 < preset.high_t60 <= preset.mid_t60 <= preset.low_t60 <= 5
            and 0 <= preset.width <= 1 and 0 <= preset.early_fraction <= 1):
        raise ValueError("Space parameters outside the supported range")
    delay = round(preset.predelay * sr)
    n = round((2 * preset.low_t60 + .2) * sr)
    t = np.arange(n) / sr
    # Drawing frames before channels keeps the noise prefix identical for
    # different IR lengths at a given sample rate and seed.
    noise = np.random.default_rng(seed).standard_normal((n, 2))
    lp_low = signal.sosfilt(signal.butter(2, 350, fs=sr, output="sos"), noise, axis=0)
    lp_mid = signal.sosfilt(signal.butter(2, 3200, fs=sr, output="sos"), noise, axis=0)
    low, mid, high = lp_low, lp_mid - lp_low, noise - lp_mid
    tail = (.8 * low * np.exp(-np.log(1000) * t[:, None] / preset.low_t60)
            + mid * np.exp(-np.log(1000) * t[:, None] / preset.mid_t60)
            + .25 * high * np.exp(-np.log(1000) * t[:, None] / preset.high_t60))
    # Smooth buildup and a deep-tail taper; both are baked into the fixed IR.
    tail *= (-np.expm1(-t / preset.buildup))[:, None]
    taper = round(.05 * sr)
    tail[-taper:] *= (.5 + .5 * np.cos(np.linspace(0, np.pi, taper)))[:, None]
    tail = signal.sosfilt(signal.butter(2, 85, "highpass", fs=sr, output="sos"), tail, axis=0)
    # Mid/side construction retains a common component when folded to mono.
    m, s = tail.T
    tail = np.column_stack((m + preset.width * s, m - preset.width * s))
    tail = _unit_energy(tail)
    early = np.zeros_like(tail)
    taps = ((0, .8, -.6), (.0073, .61, .55), (.0149, .48, -.25),
            (.0261, .36, .75), (.0387, .24, -.7), (.0543, .15, .2))
    for seconds, gain, pan in taps:
        angle = (pan * preset.width + 1) * np.pi / 4
        early[round(seconds * sr)] += gain * np.array((np.cos(angle), np.sin(angle)))
    early = signal.sosfilt(signal.butter(2, 4500, fs=sr, output="sos"), early, axis=0)
    early = _unit_energy(early)
    wet = np.sqrt(preset.early_fraction) * early + np.sqrt(1 - preset.early_fraction) * tail
    wet = _unit_energy(wet)
    return np.pad(wet, ((delay, 0), (0, 0)))


def process(x, ir, wet_gain, dry_gain=1.0):
    """Mono input to stereo output, retaining the complete convolution tail."""
    x = np.asarray(x, dtype=float)
    ir = np.asarray(ir, dtype=float)
    if (x.ndim != 1 or not len(x) or ir.ndim != 2 or ir.shape[1] != 2
            or not len(ir) or not np.isfinite(x).all() or not np.isfinite(ir).all()
            or not np.isfinite([wet_gain, dry_gain]).all()):
        raise ValueError("Expected finite nonempty mono input and stereo IR")
    wet = np.column_stack([signal.fftconvolve(x, ir[:, ch], mode="full") for ch in range(2)])
    result = wet * wet_gain
    result[:len(x)] += dry_gain * x[:, None]
    return result
