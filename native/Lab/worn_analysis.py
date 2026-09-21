"""Measure the WornTape probes; off-harmonic energy is not a perceptual score."""
from pathlib import Path
import argparse
import json
import numpy as np
import soundfile as sf
from scipy.signal import hilbert, sosfiltfilt, butter


def db(x):
    return float(20 * np.log10(max(1e-15, float(x))))


def main(root):
    report = {"levels": {}, "stationary_alias": {}, "motion": {}}
    for path in [root / "input-room.wav", *sorted((root / "renders").glob("*.wav"))]:
        x, sr = sf.read(path)
        assert np.isfinite(x).all()
        rms = np.sqrt(np.mean(x * x))
        hi = sosfiltfilt(butter(4, 6000, fs=sr, btype="high", output="sos"), x, axis=0)
        report["levels"][path.name] = {
            "rms_dbfs": db(rms), "peak_dbfs": db(np.max(np.abs(x))),
            "over_6k_relative_db": db(np.sqrt(np.mean(hi * hi)) / rms),
            "dc": np.mean(x, axis=0).tolist(),
        }
    # Last second is coherent and settled. Exclude all legal harmonics, DC,
    # and a three-bin neighbourhood. Includes numeric and resampler errors.
    # 64x provides a comparison floor, not proof of mathematically zero aliasing.
    for path in sorted((root / "probes").glob("alias-*.wav")):
        hz = int(path.stem.split("-")[1])
        x, sr = sf.read(path)
        a = np.abs(np.fft.rfft(x[-sr:, 0]))
        mask = np.ones(len(a), bool)
        mask[:4] = False
        for harmonic in range(1, int(sr / 2 / hz) + 1):
            at = hz * harmonic
            mask[at - 3:at + 4] = False
        residual_rms = np.sqrt(2) * np.linalg.norm(a[mask]) / sr
        report["stationary_alias"][path.name] = {
            "unexplained_energy_dbc": db(np.linalg.norm(a[mask]) / np.linalg.norm(a[~mask])),
            "unexplained_rms_dbfs": db(residual_rms),
            "fundamental_peak_dbfs": db(2 * a[hz] / sr),
        }
    # A low-level 3150 Hz carrier with noise disabled. Hilbert measurements
    # include filter phase changes during damage, as well as transport motion.
    for path in sorted((root / "probes").glob("motion-*.wav")):
        x, sr = sf.read(path)
        a = hilbert(x[:, 0])
        freq = np.diff(np.unwrap(np.angle(a))) * sr / (2 * np.pi)
        freq = freq[int(.5 * sr):-int(.25 * sr)]
        assert np.min(freq) > 0
        cents = 1200 * np.log2(freq / 3150)
        envelope = np.abs(a)[int(.5 * sr):-int(.25 * sr)]
        report["motion"][path.name] = {
            "pitch_cents_p01_p50_p99": np.percentile(cents, [1, 50, 99]).tolist(),
            "pitch_rms_cents": float(np.sqrt(np.mean(cents * cents))),
            "amplitude_p05_to_p95_db": db(np.percentile(envelope, 5) / np.percentile(envelope, 95)),
        }
    (root / "analysis.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    main(parser.parse_args().root)
