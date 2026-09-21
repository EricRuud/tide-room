"""Render and check the first space-focused listening round. No taste ranking."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil

ROOT = Path(__file__).resolve().parent
os.environ.setdefault("MPLCONFIGDIR", str(ROOT / ".cache" / "matplotlib"))
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import scipy
from scipy import signal
import soundfile as sf

import space

ANCHOR = ROOT / "artifacts/baseline/phrase.wav"
ANCHOR_HASH = "31837645d6b6e023f374fc2a5ed0cd9594452305cf77cd36d96cf18ac72ad595"
SEED = 208


def rms(x):
    return float(np.sqrt(np.mean(np.square(x))))


def db(x):
    return float(20 * np.log10(max(float(x), 1e-15)))


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2, allow_nan=False) + "\n")


def spatial_checks():
    """Independent steady-state Fourier response, including high-frequency probes."""
    cases = []
    n = 8192
    bins = np.array([23, 307, 2003, 3689])
    for sr in (44100, 48000):
        for preset in space.PRESETS:
            h = space.impulse_response(sr, preset, SEED)
            times = np.arange(len(h) - 1 + n)
            x = sum(.13 * np.cos(2 * np.pi * k * times / n) for k in bins)
            actual = space.process(x, h, .6)[len(h) - 1:len(h) - 1 + n]
            expected = np.zeros((n, 2))
            phase_time = times[-n:]
            for k in bins:
                response = (np.exp(-2j * np.pi * k * np.arange(len(h)) / n)[:, None] * h).sum(axis=0)
                expected += .13 * np.real(np.exp(2j * np.pi * k * phase_time / n)[:, None]
                                           * (1 + .6 * response)[None, :])
            residual_db = db(rms(actual - expected) / rms(expected))
            spectrum = np.fft.rfft(actual, axis=0)
            off = np.ones(len(spectrum), dtype=bool)
            off[bins] = False
            off_db = db(np.linalg.norm(spectrum[off]) / np.linalg.norm(spectrum))
            # Check that a nonlinear failure would not slip through the probe.
            bad = np.fft.rfft(np.tanh(4 * actual), axis=0)
            bad_off_db = db(np.linalg.norm(bad[off]) / np.linalg.norm(bad))
            tail_db = db(rms(h[-round(.05 * sr):]) / rms(h))
            cases.append({"sample_rate": sr, "preset": preset.name,
                          "frequencies_hz": (bins * sr / n).tolist(),
                          "steady_response_residual_db": residual_db,
                          "off_tone_fft_norm_ratio_db": off_db,
                          "deliberate_nonlinearity_off_tone_db": bad_off_db,
                          "ir_final_50ms_relative_rms_db": tail_db,
                          "pass": bool(residual_db < -120 and off_db < -120
                                       and bad_off_db > -80 and tail_db < -90)})
    return cases


def run(out):
    if out.exists() and any(out.iterdir()):
        raise FileExistsError(f"Refusing to overwrite {out}")
    if hashlib.sha256(ANCHOR.read_bytes()).hexdigest() != ANCHOR_HASH:
        raise ValueError("The liked anchor has changed")
    out.mkdir(parents=True, exist_ok=True)
    for directory in ("source", "raw", "impulses", "listening"):
        (out / directory).mkdir()
    for name in ("space.py", "space_round.py", "criteria.json", "requirements.txt",
                 "tests/test_space.py", "feedback/003-space-priority.json"):
        source = ROOT / name
        dest = out / "source" / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, dest)
    shutil.copyfile(ANCHOR, out / "liked-original.wav")
    x, sr = sf.read(ANCHOR)
    if x.ndim != 1 or sr != 48000 or not np.isfinite(x).all():
        raise ValueError("Unexpected anchor format")
    raw = {"anchor": np.column_stack((x, x))}
    settings = {}
    for preset in space.PRESETS:
        h = space.impulse_response(sr, preset, SEED)
        wet = space.process(x, h, 1, 0)
        # Calibrate the fixed send level once on the common source phrase.
        # This logged gain is constant; the processor has no input-dependent AGC.
        wet_gain = 10 ** (preset.wet_db / 20) * rms(x) / rms(wet[:len(x)])
        raw[preset.name] = space.process(x, h, wet_gain)
        settings[preset.name] = {**preset.settings(), "calibrated_wet_gain": wet_gain,
                                 "ir_samples": len(h), "ir_duration_seconds": len(h) / sr}
        sf.write(out / "impulses" / f"{preset.name}.wav", h, sr, subtype="FLOAT")
    for name, y in raw.items():
        sf.write(out / "raw" / f"{name}.wav", y, sr, subtype="FLOAT")
    target = 10 ** (-22 / 20)
    gains = {name: target / rms(y[:len(x)]) for name, y in raw.items()}
    matched = {name: y * gains[name] for name, y in raw.items()}
    # Common headroom adjustment, never limiting or per-frame normalization.
    largest_peak = max(float(np.max(np.abs(signal.resample_poly(y, 4, 1, axis=0))))
                       for y in matched.values())
    common_gain = min(1., .85 / largest_peak)
    matched = {name: y * common_gain for name, y in matched.items()}
    # Keep all audible tails, then use the same duration for every comparison.
    # The final 100 ms must measure below -90 dBFS before presentation fades.
    end = len(x)
    for y in matched.values():
        active = np.flatnonzero(np.max(np.abs(y), axis=1) > 10 ** (-100 / 20))
        if len(active):
            end = max(end, int(active[-1]) + round(.2 * sr))
    end = int(np.ceil(end / (sr * .1)) * round(sr * .1))
    labels = [p.name for p in space.PRESETS]
    np.random.default_rng(20260907).shuffle(labels)
    key = {"anchor": "anchor", **dict(zip("ABC", labels))}
    checks = spatial_checks()
    rendered = {}
    measurements = {}
    for label, name in key.items():
        y = matched[name]
        y = np.pad(y, ((0, max(0, end - len(y))), (0, 0)))[:end]
        tail_db = db(rms(y[-round(.1 * sr):]))
        if tail_db > -90:
            raise ValueError(f"Tail truncation too loud: {name}: {tail_db:.1f} dBFS")
        nfade = round(.01 * sr)
        y[-nfade:] *= (.5 + .5 * np.cos(np.linspace(0, np.pi, nfade)))[:, None]
        path = out / "listening" / f"{label}.wav"
        sf.write(path, y, sr, subtype="PCM_24")
        decoded, decoded_sr = sf.read(path, always_2d=True)
        if decoded_sr != sr or decoded.shape != y.shape or not np.isfinite(decoded).all():
            raise ValueError("Invalid exported comparison")
        qerror = float(np.max(np.abs(decoded - y)))
        peak = float(np.max(np.abs(decoded)))
        peak4 = float(np.max(np.abs(signal.resample_poly(decoded, 4, 1, axis=0))))
        first = decoded[:len(x)]
        mono_db = db(rms(np.mean(first, axis=1)) / rms(first))
        measurements[label] = {"file": f"listening/{label}.wav", "duration_seconds": end / sr,
                               "constant_gain": gains[name] * common_gain,
                               "matching_window_seconds": len(x) / sr,
                               "matching_window_rms_dbfs": db(rms(first)),
                               "sample_peak_dbfs": db(peak), "oversampled_peak_4x_dbfs": db(peak4),
                               "mono_fold_relative_rms_db": mono_db,
                               "stereo_correlation_matching_window": float(np.corrcoef(first.T)[0, 1]),
                               "tail_before_fade_final_100ms_dbfs": tail_db,
                               "export_max_quantization_error": qerror,
                               "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                               "pass": bool(peak < .99 and peak4 < .9 and mono_db > -6 and qerror < 2 ** -22)}
        rendered[label] = decoded
    gap = np.zeros((round(.5 * sr), 2))
    montage_parts, starts, cursor = [], {}, 0
    for label, y in rendered.items():
        starts[label] = cursor / sr
        if montage_parts:
            montage_parts.append(gap)
            cursor += len(gap)
            starts[label] = cursor / sr
        montage_parts.append(y)
        cursor += len(y)
    sf.write(out / "listen-all.wav", np.concatenate(montage_parts), sr, subtype="PCM_24")
    target_db = -22 + db(common_gain)
    matching_error = max(abs(m["matching_window_rms_dbfs"] - target_db) for m in measurements.values())
    all_pass = bool(all(c["pass"] for c in checks) and all(m["pass"] for m in measurements.values())
                    and matching_error < .001)
    report = {"created_utc": datetime.now(timezone.utc).isoformat(), "technical_pass": all_pass,
              "scope": "Fixed LTI stereo convolution stage and this four-file listening batch. "
                       "Not a perceptual match to Etch/Braid, realtime validation, or universal aliasing guarantee.",
              "anchor_sha256": ANCHOR_HASH, "sample_rate": sr, "ir_seed": SEED,
              "normalization": "Constant gain to -22 dBFS stereo-energy RMS over the common first 6.5 seconds; "
                               "same additional headroom gain for all clips. Approximate listening-level match, not LUFS.",
              "common_headroom_gain": common_gain, "rms_matching_max_error_db": matching_error,
              "tail_handling": "Full raw convolution retained. Shared listening duration includes every sample above "
                               "-100 dBFS plus 200 ms; final 10 ms cosine fade after final 100 ms is below -90 dBFS.",
              "settings": settings, "checks": checks, "listening": measurements,
              "montage_start_seconds": starts,
              "versions": {"python": platform.python_version(), "numpy": np.__version__,
                           "scipy": scipy.__version__, "soundfile": sf.__version__},
              "source_hashes": {str(p.relative_to(out / "source")): hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in (out / "source").rglob("*") if p.is_file()}}
    write_json(out / "report.json", report)
    write_json(out / "blind-key.json", key)
    write_json(out / "feedback-template.json", {
        "round": str(out.relative_to(ROOT)) if out.is_relative_to(ROOT) else str(out),
        "priority": "overall sense of space; note texture and decay also matter",
        "closest_to_desired_space": None, "keep_from_other_versions": None,
        "too_wet_distant_or_blurred": None, "notes": "Anchor, A, B, C, ties, or none are all valid."})
    lines = ["# Space listening round 1", "",
             "User priority: overall sense of space, with note texture and changing decay also important. "
             "Etch is the primary musical reference and Braid the second example.", "",
             "[Play all: anchor → A → B → C](listen-all.wav)", "",
             " | Clip | Start in montage | Individual audio |", " | --- | ---: | --- |"]
    for label in key:
        lines.append(f'| {label} | {starts[label]:.1f} s | [{label}](listening/{label}.wav) |')
    lines += ["", "Every clip uses the exact liked prototype phrase as its source. "
              "The three variants explore different reflections, buildup, decay, and stereo width. "
              "Their identities are saved separately in `blind-key.json`. "
              "These are original spatial sketches, not reconstructions of the records.", "",
              "Listen at a comfortable fixed volume, preferably with stereo headphones or speakers. "
              "Which has the most desirable space? Does any lose too much attack or become too distant? "
              "What would you keep from another version? Ties and none are useful answers.", "",
              "## Validation", "", f'Technical checks: **{"PASS" if all_pass else "FAIL"}**.', "",
              "- Steady-state output agrees with independently calculated filter frequency responses at "
              "44.1 and 48 kHz; probes include high frequencies. Off-tone components remain below -120 dB "
              "in these probes. A deliberate nonlinear control fails this criterion.",
              "- Export checks cover finite output, sample and 4x oversampled peaks, quantization, "
              "constant-gain level matching, tail completion, and mono fold-down energy.",
              "- The spatial processor is fixed and linear. No new saturation or moving delay is introduced; "
              "the source phrase and its existing limits remain those of the liked baseline.",
              "- Fixed finite convolution cannot self-oscillate. This is an offline FIR experiment; "
              "streaming CPU, block processing, automation, and plugin-host behavior have not been evaluated.",
              "", f'RMS matching differs by at most {matching_error:.6f} dB over the common 6.5-second window. '
              "This does not establish equal perceived loudness. No compressor or limiter is used.", "",
              "[Envelope and stereo overview](diagnostics.png). Full measurements and settings are in "
              "`report.json` (reveals design settings). The unmodified original is `liked-original.wav`; "
              "raw renders, synthetic impulses, and source snapshots are included.", "",
              "Method background: [Julius O. Smith, Late Reverberation Approximations]"
              "(https://www.dsprelated.com/freebooks/pasp/Late_Reverberation_Approximations.html). "
              "The particular settings and sounds here are original design hypotheses awaiting listening feedback."]
    (out / "report.md").write_text("\n".join(lines) + "\n")
    fig, axes = plt.subplots(2, 1, figsize=(10, 7), constrained_layout=True)
    frame = round(.02 * sr)
    for label, y in rendered.items():
        count = len(y) // frame
        env = np.sqrt(np.mean(y[:count * frame].reshape(count, frame, 2) ** 2, axis=(1, 2)))
        axes[0].plot((np.arange(count) + .5) * .02, 20 * np.log10(np.maximum(env, 1e-7)), label=label)
    axes[0].set(xlabel="Time (s)", ylabel="20 ms RMS (dBFS)", ylim=(-100, 0),
                title="Same original phrase — shared level window, complete quiet tails")
    axes[0].legend()
    axes[1].bar(list(measurements), [m["mono_fold_relative_rms_db"] for m in measurements.values()], color="#245d77")
    axes[1].set(ylabel="Mono / stereo RMS (dB)", ylim=(-6, .5),
                title="Mono fold-down energy over the first 6.5 seconds — descriptive, not a taste score")
    fig.savefig(out / "diagnostics.png", dpi=140)
    plt.close(fig)
    if not all_pass:
        raise RuntimeError(f"Listening batch failed technical checks: {out}")
    print(out)
    print(json.dumps({"technical_pass": all_pass, "starts": starts, "listening": measurements}, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    output = args.out or ROOT / "artifacts" / ("space-round-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ"))
    run(output.resolve())
