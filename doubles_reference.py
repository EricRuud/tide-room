"""Describe the selected album streams; no taste, aliasing, or guitar-isolation score."""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import os
import platform

ROOT = Path(__file__).resolve().parent
os.environ.setdefault("MPLCONFIGDIR", str(ROOT / ".cache" / "matplotlib"))
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import scipy
from scipy import signal
import soundfile as sf


def main():
    provenance_path = ROOT / "references/doubles-provenance.json"
    provenance = json.loads(provenance_path.read_text())
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    out = ROOT / "artifacts" / ("doubles-reference-" + run_id)
    out.mkdir(parents=True, exist_ok=False)
    (out / "doubles_reference.py").write_bytes(Path(__file__).read_bytes())
    (out / "provenance.json").write_bytes(provenance_path.read_bytes())
    fig, axes = plt.subplots(len(provenance["files"]), 2, figsize=(12, 7),
                             squeeze=False, constrained_layout=True)
    measurements = []
    lines = ["# Etch and Braid: initial reference inventory", "",
             "Etch is the user's primary reference; Braid is the secondary example.", "",
             "These are measurements of public MP3 streams of the studio album, not a listening verdict. "
             "They describe the combined recording, including instruments, effects, mix processing, and encoding. "
             "No source separation, harmonic attribution, or aliasing estimate is attempted.", "",
             "[Envelope and spectrum overview](overview.png)", "",
             "| Track | Duration | RMS | Sample peak | Crest factor |",
             "| --- | ---: | ---: | ---: | ---: |"]
    for row, item in enumerate(provenance["files"]):
        path = ROOT / item["path"]
        if hashlib.sha256(path.read_bytes()).hexdigest() != item["sha256"]:
            raise ValueError(f"Reference hash changed: {path}")
        y, sr = sf.read(path, always_2d=True)
        if not len(y) or not np.isfinite(y).all() or not np.any(y):
            raise ValueError(f"Empty, non-finite, or silent reference: {path}")
        if sr != item["sample_rate"] or len(y) != item["frames"]:
            raise ValueError(f"Reference encoding differs from provenance: {path}")
        rms = float(np.sqrt(np.mean(y * y)))
        peak = float(np.max(np.abs(y)))
        frame = round(.01 * sr)
        count = len(y) // frame
        # Average channel powers, never waveforms: antiphase stereo cannot cancel.
        envelope = np.sqrt(np.mean(y[:count * frame].reshape(count, frame, -1) ** 2,
                                   axis=(1, 2)))
        et = (np.arange(count) + .5) * frame / sr
        env_db = 20 * np.log10(np.maximum(envelope, 1e-12))
        axes[row, 0].plot(et, env_db, linewidth=.65, color="#245d77")
        axes[row, 0].set(title=f'{item["title"]} — 10 ms stereo-energy envelope',
                         xlabel="Time (s)", ylabel="RMS (dBFS)", ylim=(-70, 0))
        power = None
        for ch in range(y.shape[1]):
            f, t, p = signal.spectrogram(y[:, ch], sr, window="hann", nperseg=2048,
                                         noverlap=0, detrend=False, scaling="density")
            if power is None:
                power = p / y.shape[1]
            else:
                power += p / y.shape[1]
        spectrum = 10 * np.log10(np.maximum(power, 1e-14))
        plot = axes[row, 1].pcolormesh(t, f, spectrum, shading="auto", cmap="magma",
                                     vmin=-110, vmax=-40, rasterized=True)
        axes[row, 1].set(title=f'{item["title"]} — combined recording spectrum',
                         xlabel="Time (s)", ylabel="Frequency (Hz)",
                         yscale="log", ylim=(60, 12000))
        fig.colorbar(plot, ax=axes[row, 1], label="Power density (dBFS/Hz)")
        # Evenly spaced orientation windows, chosen without judging their sound.
        # They are listening starting points, not isolated-note measurements.
        duration = len(y) / sr
        windows = [{"start_seconds": int(duration * fraction),
                    "end_seconds": int(duration * fraction) + 20,
                    "selection": "Position-based orientation window; not auditioned"}
                   for fraction in (.10, .45, .75)]
        data = {"title": item["title"], "role": item["role"],
                "source_page": item["source_page"], "sha256": item["sha256"],
                "sample_rate": sr, "channels": y.shape[1], "duration_seconds": duration,
                "rms_dbfs": float(20 * np.log10(rms)),
                "sample_peak_dbfs": float(20 * np.log10(peak)),
                "crest_factor_db": float(20 * np.log10(peak / rms)),
                "samples_at_or_above_full_scale": int(np.count_nonzero(np.abs(y) >= 1)),
                "envelope_window_seconds": frame / sr,
                "envelope_time_seconds": et.tolist(), "envelope_rms_dbfs": env_db.tolist(),
                "orientation_windows": windows}
        measurements.append(data)
        lines.append(f'| {item["title"]} | {duration:.2f} s | {data["rms_dbfs"]:.2f} dBFS | '
                     f'{data["sample_peak_dbfs"]:.2f} dBFS | {data["crest_factor_db"]:.2f} dB |')
    fig.suptitle("User-selected album references — descriptive measurements of lossy streams")
    fig.savefig(out / "overview.png", dpi=140)
    plt.close(fig)
    lines += ["", "Crest factor is sample peak divided by whole-track RMS, expressed in dB. "
              "It is not a score for musical dynamics. Sample peaks do not establish true peak or prior clipping.",
              "", "## Starting points for listening", "",
              "These 20-second windows are spaced across each track by position. "
              "They have not been auditioned or selected as the best examples. "
              "Use the original album players; the later live Etch performance is a separate reference.", ""]
    for item in measurements:
        spans = [f'{w["start_seconds"] // 60}:{w["start_seconds"] % 60:02d}–'
                 f'{w["end_seconds"] // 60}:{w["end_seconds"] % 60:02d}'
                 for w in item["orientation_windows"]]
        lines.append(f'- [{item["title"]}]({item["source_page"]}): ' + ", ".join(spans))
    lines += ["", "## First sound-design round", "",
              "Use one short original phrase and a few isolated notes for every candidate. "
              "Keep the liked baseline as the anchor. Change one aspect at a time:", "",
              "1. Attack and body: vary the brief excitation while keeping the sustained tone comparable.",
              "2. Harmonic decay: let upper partials fade at different rates; keep pitch modulation separate.",
              "3. Continuity: test a restrained resonant tail that overlaps the next note.", "",
              "Present three candidates and the anchor with logged constant-gain level matching. "
              "Ask which is closest to the desired quality, which feels overdone, and what should be kept. "
              "Ties and 'none' are valid. These hypotheses are not yet verified descriptions of Etch or Braid.", "",
              "Any new DSP needs its own applicable technical checks; the baseline's passing results "
              "do not transfer automatically. Numerical taste targets remain unset until listening feedback supports them."]
    report = {"status": "descriptive reference inventory; musical interpretation pending",
              "created_utc": datetime.now(timezone.utc).isoformat(),
              "method": {"channel_combination": "mean of channel powers",
                         "spectrogram_window": "2048-sample Hann, no overlap",
                         "spectrogram_scaling": "power spectral density, no detrending",
                         "normalization": "none"},
              "versions": {"python": platform.python_version(), "numpy": np.__version__,
                           "scipy": scipy.__version__, "soundfile": sf.__version__,
                           "libsndfile": sf.__libsndfile_version__, "matplotlib": matplotlib.__version__},
              "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "files": measurements}
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    (out / "report.md").write_text("\n".join(lines) + "\n")
    print(out.relative_to(ROOT))
    for item in measurements:
        print(f'{item["title"]}: {item["duration_seconds"]:.2f} s, '
              f'{item["rms_dbfs"]:.2f} dBFS RMS, '
              f'{item["crest_factor_db"]:.2f} dB crest factor')


if __name__ == "__main__":
    main()
