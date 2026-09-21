"""An offline sine-wavefolder test voice, NOT a Buchla circuit emulation.

The analytic control trajectories are evaluated at the internal sample rate.
The long linear-phase FIR is an offline laboratory choice, not a realtime design.
"""
from functools import lru_cache

import numpy as np
from scipy import signal, special

BIAS = 0.12
GAIN = 0.35
PAD = 256


def fold(phase, drive, bias=BIAS):
    # Remove the instantaneous phase-average DC analytically. This is part of
    # this test voice's definition, not a claimed model of an analog DC blocker.
    return GAIN * (np.sin(drive * np.sin(phase) + bias)
                   - np.sin(bias) * special.j0(drive))


@lru_cache(maxsize=16)
def decimation_filter(factor):
    return signal.firwin(384 * factor + 1, 0.90 / factor,
                         window=("kaiser", 14.0))


def render_function(fn, n, sr, factor):
    if factor not in (1, 2, 4, 8, 16, 32, 64):
        raise ValueError("Unsupported oversampling factor")
    t = np.arange(-PAD * factor, (n + PAD) * factor, dtype=float) / (sr * factor)
    y = fn(t)
    if factor > 1:
        y = signal.resample_poly(y, 1, factor, window=decimation_filter(factor))
    return np.asarray(y[PAD:PAD + n], dtype=np.float64)


def stationary(n, sr, factor, frequency, drive):
    return render_function(lambda t: fold(2 * np.pi * frequency * t, drive),
                           n, sr, factor)


def motion_drive(t, depth=0.7, seed=208, stepped=False):
    if stepped:
        t = np.floor(t * 12) / 12  # Deliberate bad 12 Hz control update.
    phase = (seed % 997) * 0.013
    return 2.7 + depth * (0.65 * np.sin(2 * np.pi * .41 * t + phase)
                          + .35 * np.sin(2 * np.pi * .73 * t - phase))


def held(n, sr, factor, depth=0.7, seed=208, stepped=False):
    return render_function(
        lambda t: fold(2 * np.pi * 220 * t,
                       motion_drive(t, depth, seed, stepped)), n, sr, factor)


def phrase(sr=48000, factor=8, depth=0.7, seed=208):
    """A short original listening example; overlapping decays retain tails."""
    pitches = [110, 164.8138, 220, 146.8324, 195.9977, 130.8128]
    velocities = [.6, .85, .7, 1., .65, .9]

    def source(t):
        out = np.zeros_like(t)
        for i, (pitch, velocity) in enumerate(zip(pitches, velocities)):
            age = np.maximum(t - i * .8, 0)
            env = velocity * (-np.expm1(-age / .006)) * np.exp(-age / .30)
            drive = 1.1 + 2.9 * env + depth * .22 * np.sin(.8 * t + seed)
            # Harmonic richness falls with the envelope. This is not an LPG.
            out += env * fold(2 * np.pi * pitch * age, drive)
        return out

    return render_function(source, int(6.5 * sr), sr, factor)
