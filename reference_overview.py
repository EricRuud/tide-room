"""Reproduce the four-reference envelope/spectrum figure; no taste scoring."""
from pathlib import Path
import os

ROOT = Path(__file__).resolve().parent
os.environ.setdefault("MPLCONFIGDIR", str(ROOT / ".cache" / "matplotlib"))
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy import signal
import soundfile as sf


def main():
    names = ["Conga Big_bip.wav", "Tabla 1_bip.wav", "Wood Block_bip.wav", "Cosmic Clay_bip.wav"]
    fig, axes = plt.subplots(4, 2, figsize=(11, 10), constrained_layout=True)
    for row, name in enumerate(names):
        y, sr = sf.read(ROOT / "references" / "audio" / "omadowin" / name, always_2d=True)
        size = round(.01 * sr)
        count = len(y) // size
        env = np.sqrt(np.mean(y[:count * size].reshape(count, size, -1) ** 2, axis=(1, 2)))
        axes[row, 0].plot(np.arange(count) * .01, 20 * np.log10(np.maximum(env, 1e-6)))
        axes[row, 0].set(title=name.replace("_bip.wav", ""), xlabel="Time (s)", ylabel="RMS (dBFS)", ylim=(-90, 0))
        f, t, z = signal.stft(y, sr, nperseg=512, noverlap=448, axis=0, boundary=None)
        power = np.mean(np.abs(z) ** 2, axis=1)
        spec = 10 * np.log10(np.maximum(power, 1e-12))
        axes[row, 1].pcolormesh(t, f, spec, shading="auto", vmin=-90, vmax=-15, cmap="magma")
        axes[row, 1].set(xlabel="Time (s)", ylabel="Frequency (Hz)", ylim=(0, 12000))
    fig.suptitle("Buchla reference inventory — measured envelopes and spectra, no listening verdict")
    fig.savefig(ROOT / "references" / "overview.png", dpi=130)
    plt.close(fig)


if __name__ == "__main__":
    main()
