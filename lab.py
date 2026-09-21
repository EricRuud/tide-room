"""Repeatable offline render/evaluate/search harness. Run: python3 lab.py run."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import time
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parent
os.environ.setdefault("MPLCONFIGDIR", str(ROOT / ".cache" / "matplotlib"))
import numpy as np
from scipy import signal, special
import scipy
import soundfile as sf

import voice


def rms(x):
    return float(np.sqrt(np.mean(np.square(x))))


def db(value):
    return float(20 * np.log10(max(float(value), 1e-15)))


def band_rms(x, sr, upper):
    """Exact one-sided Parseval energy; signals must be coherently sampled."""
    n = len(x)
    spectrum = np.fft.rfft(x) / n
    weights = np.full(len(spectrum), 2.)
    weights[0] = 1.
    if n % 2 == 0:
        weights[-1] = 1.
    mask = np.fft.rfftfreq(n, 1 / sr) <= upper
    return float(np.sqrt(np.sum(weights[mask] * np.abs(spectrum[mask]) ** 2)))


def analytic_reference(n, sr, frequency, drive):
    """Independent Jacobi-Anger expansion, evaluated only below Nyquist.

    For configured drive <= 9, the omitted Bessel tail above order 128 is
    negligible. No sampled/aliased wavefolder output is used to build this.
    """
    if not 0 < drive <= 9:
        raise ValueError("Analytic reference is validated only for drive in (0, 9]")
    phase = 2 * np.pi * frequency * np.arange(n) / sr
    y = np.zeros(n)
    for k in range(1, min(128, int(np.ceil(sr / (2 * frequency))) - 1) + 1):
        if k % 2:
            y += 2 * np.cos(voice.BIAS) * special.jv(k, drive) * np.sin(k * phase)
        else:
            y += 2 * np.sin(voice.BIAS) * special.jv(k, drive) * np.cos(k * phase)
    return voice.GAIN * y


def residual(candidate, reference, sr=None, upper=None):
    if candidate.shape != reference.shape:
        raise ValueError("Residual inputs must have identical shape")
    if not np.all(np.isfinite(candidate)) or not np.all(np.isfinite(reference)):
        raise ValueError("Non-finite audio")
    measure = (lambda x: band_rms(x, sr, upper)) if upper else rms
    reference_rms = measure(reference)
    if reference_rms < 1e-12:
        raise ValueError("Reference is too quiet for a relative measurement")
    error = measure(candidate - reference)
    return {"relative_db": db(error / reference_rms), "absolute_dbfs": db(error)}


def harmonic_motion(y, sr, frequency=220., harmonics=8):
    """Descriptive only: dispersion of normalized harmonic amplitudes.

    Fixed known pitch, Hann-window sinusoidal projection every 50 ms over
    100 ms. No inference of 'alive', analog authenticity, or human preference.
    """
    n, hop = round(.1 * sr), round(.05 * sr)
    if len(y) < n or frequency * harmonics >= .4 * sr:
        raise ValueError("Clip too short or harmonics not resolvable")
    frames = np.lib.stride_tricks.sliding_window_view(y, n)[::hop]
    window = signal.windows.hann(n, sym=False)
    t = np.arange(n) / sr
    basis = np.exp(-2j * np.pi * frequency * np.arange(1, harmonics + 1)[:, None] * t)
    amps = np.abs((frames * window) @ basis.T) * 2 / np.sum(window)
    valid = (np.sqrt(np.mean(frames ** 2, axis=1)) > 1e-7) & (amps.sum(axis=1) > 1e-7)
    if np.sum(valid) < 3:
        return {"valid": False, "dispersion": None, "reason": "insufficient voiced signal"}
    proportions = amps[valid] / amps[valid].sum(axis=1, keepdims=True)
    spread = np.sqrt(np.mean(np.sum((proportions - proportions.mean(axis=0)) ** 2, axis=1)))
    return {"valid": True, "dispersion": float(spread),
            "times": (np.arange(len(frames))[valid] * hop / sr + .05).tolist(),
            "harmonic_proportions": proportions.tolist(), "harmonics": harmonics,
            "interpretation": "spectral movement descriptor; preference uncalibrated"}


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, allow_nan=False) + "\n")


def load_criteria(path):
    c = json.loads(path.read_text())
    if c["motion_target_range"] is not None:
        raise ValueError("v0 does not gate on unvalidated motion targets; leave range null")
    if c["fft_size"] < 1024 or c["fft_size"] % 2:
        raise ValueError("fft_size must be even and >= 1024")
    if not 0 < c["measurement_band_fraction_of_sample_rate"] <= .4:
        raise ValueError("Measurement band must be within the validated FIR passband")
    if any(not 0 < d <= 9 for d in c["drive_values"]):
        raise ValueError("Drive tests must lie in (0, 9]")
    if not c["pitch_bins"] or any(not 0 < k < c["fft_size"] / 2 for k in c["pitch_bins"]):
        raise ValueError("Pitch bins must be positive and below Nyquist")
    if not c["sample_rates"] or min(c["sample_rates"]) < 44100:
        raise ValueError("This initial harness requires sample rates >= 44100")
    if not c["oversampling_candidates"] or any(k not in [1, 2, 4, 8, 16] for k in c["oversampling_candidates"]):
        raise ValueError("Invalid oversampling candidates")
    if not 0 <= c["motion_depth"] <= 2:
        raise ValueError("Motion depth must be in [0, 2]")
    return c


def stationary_suite(c, factor):
    cases = []
    n = c["fft_size"]
    for sr in c["sample_rates"]:
        for pitch_bin in c["pitch_bins"]:
            frequency = pitch_bin * sr / n
            for drive in c["drive_values"]:
                actual = voice.stationary(n, sr, factor, frequency, drive)
                reference = analytic_reference(n, sr, frequency, drive)
                e = residual(actual, reference, sr, c["measurement_band_fraction_of_sample_rate"] * sr)
                cases.append({"sample_rate": sr, "frequency_hz": frequency, "drive": drive,
                              "relative_residual_db": e["relative_db"],
                              "absolute_residual_dbfs": e["absolute_dbfs"],
                              "rms_dbfs": db(rms(actual)), "peak": float(np.max(np.abs(actual)))})
    return cases


def safety(y, c):
    return bool(np.all(np.isfinite(y)) and db(rms(y)) >= c["min_output_rms_dbfs"]
                and np.max(np.abs(y)) <= c["max_output_peak"])


def level_matched(y, target_db):
    level = rms(y)
    if level < 1e-12:
        return y.copy(), 0.
    gain = min(10 ** (target_db / 20) / level, .95 / np.max(np.abs(y)))
    return y * gain, float(gain)


def run(cpath, out):
    if out.exists() and any(out.iterdir()):
        raise FileExistsError(f"Preserving previous run in {out}; choose a new --out directory")
    source_data = {p.name: p.read_bytes() for p in
                   [ROOT / "voice.py", ROOT / "lab.py", ROOT / "requirements.txt", cpath]}
    c = load_criteria(cpath)
    out.mkdir(parents=True, exist_ok=True)
    source_dir = out / "source"
    source_dir.mkdir()
    for name, data in source_data.items():
        (source_dir / name).write_bytes(data)
    sr = 48000
    n = 6 * sr
    seed = c["seed"]
    # The reference is unchanged by each candidate's quality controls.
    reference32 = voice.held(n, sr, 32, c["motion_depth"], seed)
    reference64 = voice.held(n, sr, 64, c["motion_depth"], seed)
    convergence = residual(reference32, reference64)["relative_db"]
    if convergence > c["max_reference_convergence_db"]:
        raise RuntimeError(f"Dynamic reference inconclusive: convergence {convergence:.1f} dB")
    configs = [{"name": f"candidate_{factor}x", "oversampling": factor,
                "depth": c["motion_depth"], "stepped": False, "control": False}
               for factor in c["oversampling_candidates"]]
    configs += [
        {"name": "control_static", "oversampling": 8, "depth": 0., "stepped": False, "control": True},
        {"name": "control_stepped", "oversampling": 8, "depth": c["motion_depth"], "stepped": True, "control": True},
        {"name": "control_excessive_motion", "oversampling": 8, "depth": 1.9, "stepped": False, "control": True},
    ]
    results, audio = [], {}
    stationary_cache = {}
    for config in configs:
        factor = config["oversampling"]
        if factor not in stationary_cache:
            stationary_cache[factor] = stationary_suite(c, factor)
        cases = stationary_cache[factor]
        started = time.perf_counter()
        y = voice.held(n, sr, factor, config["depth"], seed, config["stepped"])
        seconds = time.perf_counter() - started
        # Static and excessive-motion controls are compared to their OWN
        # smooth intended trajectory. Wrong timbre is not called a click.
        if config["depth"] == c["motion_depth"]:
            ref = reference64
            ref_conv = convergence
        else:
            ref = voice.held(n, sr, 64, config["depth"], seed)
            ref_low = voice.held(n, sr, 32, config["depth"], seed)
            ref_conv = residual(ref_low, ref)["relative_db"]
        control_error = residual(y, ref)["relative_db"]
        worst = max(cases, key=lambda a: a["relative_residual_db"])
        motion = harmonic_motion(y, sr)
        technical = bool(worst["relative_residual_db"] <= c["max_stationary_reference_residual_db"]
                         and control_error <= c["max_control_reference_residual_db"]
                         and ref_conv <= c["max_reference_convergence_db"] and safety(y, c)
                         and all(case["rms_dbfs"] >= c["min_output_rms_dbfs"]
                                 and case["peak"] <= c["max_output_peak"] for case in cases))
        results.append({**config, "technical_pass": technical, "musical_approval": "not evaluated",
                        "worst_stationary": worst, "stationary_cases": cases,
                        "control_reference_residual_db": control_error,
                        "reference_convergence_db": ref_conv,
                        "render_seconds": seconds, "offline_seconds_per_audio_second": seconds / 6,
                        "motion": motion})
        audio[config["name"]] = y
        print(f'{config["name"]}: technical={technical}, stationary={worst["relative_residual_db"]:.1f} dB, '
              f'control={control_error:.1f} dB, motion={motion["dispersion"]:.4f}', flush=True)
    passing = [r for r in results if r["technical_pass"] and not r["control"]]
    # This selects an implementation quality setting, not the "best sounding" patch.
    selected = min(passing, key=lambda r: r["oversampling"])["name"] if passing else None
    named = {r["name"]: r for r in results}
    checks = {
        "naive_aliasing_rejected": max(stationary_suite(c, 1), key=lambda a: a["relative_residual_db"])["relative_residual_db"] > c["max_stationary_reference_residual_db"],
        "stepped_control_rejected": not named["control_stepped"]["technical_pass"],
        "static_has_less_motion": named["control_static"]["motion"]["dispersion"] < named["control_stepped"]["motion"]["dispersion"],
        "silence_rejected": not safety(np.zeros(sr), c),
        "very_quiet_output_rejected": not safety(audio["control_static"] * .001, c),
        "motion_is_gain_invariant": abs(harmonic_motion(audio["control_stepped"] * .1, sr)["dispersion"]
                                       - named["control_stepped"]["motion"]["dispersion"]) < 1e-8,
    }
    raw_dir = out / "raw"
    blind_dir = out / "listening"
    raw_dir.mkdir(exist_ok=True)
    blind_dir.mkdir(exist_ok=True)
    blind_order = np.random.default_rng(seed).permutation(list(audio))
    blind_key = []
    for i, name in enumerate(blind_order):
        sf.write(raw_dir / f"{name}.wav", audio[name], sr, subtype="FLOAT")
        y, gain = level_matched(audio[name], c["listening_rms_dbfs"])
        # Presentation-only fades. Raw engineering files are unchanged.
        fade = np.sin(np.linspace(0, np.pi / 2, 480)) ** 2
        y[:480] *= fade
        y[-480:] *= fade[::-1]
        label = f"{chr(65+i)}"
        sf.write(blind_dir / f"{label}.wav", y, sr, subtype="PCM_24")
        blind_key.append({"label": label, "candidate": name, "constant_gain": gain})
    if selected:
        factor = named[selected]["oversampling"]
        musical, gain = level_matched(voice.phrase(sr, factor, c["motion_depth"], seed), c["listening_rms_dbfs"])
        sf.write(out / "phrase.wav", musical, sr, subtype="PCM_24")
        phrase_info = {"path": "phrase.wav", "constant_gain": gain,
                       "status": "original listening illustration; no automated musical-quality verdict"}
    else:
        phrase_info = None
    report = {"created_utc": datetime.now(timezone.utc).isoformat(), "criteria": c,
              "source_sha256": {name: hashlib.sha256(data).hexdigest() for name, data in source_data.items()},
              "versions": {"python": platform.python_version(), "numpy": np.__version__, "scipy": scipy.__version__},
              "reference_convergence_db": convergence, "evaluator_checks": checks,
              "selected_technical_candidate": selected, "musical_status": "needs user calibration",
              "scope": "offline sine wavefolder; stationary band <= 0.4*sample rate; held control trajectory at 48 kHz",
              "phrase": phrase_info, "results": results}
    write_json(out / "report.json", report)
    write_json(out / "blind-key.json", blind_key)
    make_plot(report, out)
    make_report(report, out)
    if not all(checks.values()):
        raise RuntimeError("Evaluator calibration control failed; see report")
    if not selected:
        raise RuntimeError("No candidate passed; criteria were not relaxed")
    print(f"Selected {selected} on technical gates; musical judgment remains open. Report: {out / 'report.md'}")


def make_plot(report, out):
    import matplotlib
    matplotlib.use("Agg")
    from matplotlib import pyplot as plt
    fig, axes = plt.subplots(2, 1, figsize=(10, 7), constrained_layout=True)
    for r in report["results"]:
        if not r["control"]:
            cases = [q for q in r["stationary_cases"] if q["sample_rate"] == 48000]
            freqs = sorted({q["frequency_hz"] for q in cases})
            values = [max(q["relative_residual_db"] for q in cases if q["frequency_hz"] == f) for f in freqs]
            axes[0].plot(freqs, values, "o-", label=r["name"])
    axes[0].axhline(report["criteria"]["max_stationary_reference_residual_db"], ls="--", color="black", label="Provisional limit")
    axes[0].set(xscale="log", xlabel="Fundamental frequency (Hz)", ylabel="Relative residual (dB)", title="Stationary wavefolder test, 48 kHz — worst drive at each pitch")
    axes[0].legend(ncol=3, fontsize=8)
    selected = report["selected_technical_candidate"]
    r = next((x for x in report["results"] if x["name"] == selected), report["results"][0])
    if r["motion"]["valid"]:
        proportions = np.asarray(r["motion"]["harmonic_proportions"])
        for h in range(5):
            axes[1].plot(r["motion"]["times"], proportions[:, h], label=f"H{h+1}")
        axes[1].legend(ncol=5)
    axes[1].set(xlabel="Time (s)", ylabel="Share of harmonic amplitude", title=f"{r['name']}: measured motion, not a taste score")
    fig.savefig(out / "diagnostics.png", dpi=140)
    plt.close(fig)


def make_report(report, out):
    lines = ["# First evaluation run", "", "An original sine-wavefolder prototype, not a Buchla emulation.", "",
             f"Technical selection: **{report['selected_technical_candidate']}**. Musical approval: **needs user calibration**.", "",
             "| Version | Technical gates | Stationary residual (dB) | Control residual (dB) | Harmonic motion |",
             "| --- | --- | ---: | ---: | ---: |"]
    for r in report["results"]:
        lines.append(f"| {r['name']} | {'PASS' if r['technical_pass'] else 'FAIL'} | {r['worst_stationary']['relative_residual_db']:.1f} | {r['control_reference_residual_db']:.1f} | {r['motion']['dispersion']:.4f} |")
    lines += ["", "The lowest oversampling setting that passes the provisional technical gates is selected. Motion is descriptive and is not used to select a musical winner.", "",
              f"32x versus 64x dynamic-reference convergence: {report['reference_convergence_db']:.1f} dB relative RMS.", "",
              "![Diagnostic plots](diagnostics.png)", "", "## Listen", "",
              "[Original phrase](phrase.wav). Blind held-tone comparisons: " + ", ".join(f"[{p.stem}](listening/{p.name})" for p in sorted((out / 'listening').glob('*.wav'))) + ".",
              "", "Listen for desired harmonic movement, distracting wobble, rough transitions, and changes you actually prefer. Files have a constant RMS-matching gain (not LUFS matching) and presentation fades. Raw float WAVs are in `raw/`. `blind-key.json` reveals identities.", "",
              "## Scope and limitations", "",
              "Stationary testing uses an independent Bessel-series reference and coherent tones at 44.1/48 kHz. Results cover the configured pitches and drives, up to 0.4 times the sample rate (17.64/19.2 kHz). They include filter error as well as aliasing and do not prove zero aliasing everywhere.", "",
              "The control test compares one held note with a specified smooth drive trajectory. It detects the intentionally stepped control; it is not yet a general note/retrigger/LPG dynamics test. The dynamic reference shares the source equations and therefore cannot detect every model error.", "",
              "No circuit fidelity, perceptual preference, realtime safety, plugin compatibility, or reference-recording match has been established. The long symmetric FIR and padding are offline conveniences. CPU timings describe offline Python rendering, not realtime plugin CPU.", "",
              "## Evaluator controls", ""]
    lines += [f"- {'PASS' if value else 'FAIL'}: {name}" for name, value in report["evaluator_checks"].items()]
    lines += ["", "Full criteria, per-case results, source hashes, and runtime versions are in [report.json](report.json). Code and configuration snapshots are in `source/`."]
    (out / "report.md").write_text("\n".join(lines) + "\n")


def inventory(path, out):
    items = []
    for p in sorted(path.rglob("*")):
        if p.suffix.lower() not in (".wav", ".flac", ".aif", ".aiff"):
            continue
        y, sr = sf.read(p, always_2d=True)
        if not len(y) or not np.all(np.isfinite(y)):
            raise ValueError(f"Invalid reference: {p}")
        # Use channel energy, not an averaged stereo waveform that can cancel.
        frame = max(1, round(.01 * sr))
        count = len(y) // frame
        envelope = np.sqrt(np.mean(y[:count * frame].reshape(count, frame, -1) ** 2, axis=(1, 2))) if count else np.array([])
        info = sf.info(p)
        active = np.flatnonzero(np.max(np.abs(y), axis=1) > 1e-8)
        last = int(active[-1]) + 1 if len(active) else 0
        tail_db = db(rms(y[max(0, last-frame):last])) if last else -300.
        items.append({"path": str(p), "sha256": hashlib.sha256(p.read_bytes()).hexdigest(),
                      "sample_rate": sr, "channels": y.shape[1], "subtype": info.subtype,
                      "duration_seconds": len(y) / sr, "rms_dbfs": db(rms(y)),
                      "peak_dbfs": db(np.max(np.abs(y))),
                      "samples_at_or_above_full_scale": int(np.sum(np.abs(y) >= 1)),
                      "last_nonzero_seconds": last / sr,
                      "last_active_10ms_rms_dbfs": tail_db,
                      "tail_truncation_candidate": bool(tail_db > -60),
                      "tail_flag_meaning": "signal remains above -60 dBFS RMS near its end; inspect before fitting a decay",
                      "envelope_10ms_rms": envelope.tolist(),
                      "aliasing_status": "not inferable from an uncontrolled recording"})
    write_json(out, {"status": "descriptive inventory, not a listening review or emulation target", "files": items})
    print(f"Inventoried {len(items)} references in {out}")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="command", required=True)
    r = sub.add_parser("run")
    r.add_argument("--criteria", type=Path, default=ROOT / "criteria.json")
    r.add_argument("--out", type=Path, default=ROOT / "artifacts" / datetime.now(timezone.utc).strftime("run-%Y%m%dT%H%M%S%fZ"))
    i = sub.add_parser("inventory")
    i.add_argument("path", type=Path)
    i.add_argument("--out", type=Path, default=ROOT / "references" / "inventory.json")
    a = p.parse_args()
    if a.command == "run":
        run(a.criteria, a.out)
    else:
        inventory(a.path, a.out)


if __name__ == "__main__":
    main()
