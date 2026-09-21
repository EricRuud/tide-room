"""Local two-carrier response and conditioned-impulse diagnostics."""
import json
import os
os.environ.setdefault('MPLCONFIGDIR', '/private/tmp/tide-mpl')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from scipy import ndimage, optimize

from p821_identification_analysis import ROOT, SR, read, linear_kernel, db


def main():
    y = (read('envelope-plus') - read('envelope-minus'))*.5
    p = read('envelope-probe')
    t = np.arange(len(y))/SR
    cases = json.loads((ROOT/'envelope-cases.json').read_text())
    curves = {}
    # Identical smoothing on reference and driven responses. Sigma 0.4 ms:
    # useful time resolution, but not a claim of sub-millisecond detector accuracy.
    for f in [1000, 8000]:
        osc = np.exp(-2j*np.pi*f*t)[:, None]
        a = ndimage.gaussian_filter1d(y*osc, SR*.0004, axis=0)
        b = ndimage.gaussian_filter1d(p*osc, SR*.0004, axis=0)
        curves[f] = a / np.where(abs(b)>1e-12, b, 1e-12)
    rows = []
    fig, axs = plt.subplots(2, 2, figsize=(12, 8), layout='constrained')
    for case in cases:
        for fi, f in enumerate([1000, 8000]):
            row = case | {'probe_hz': f}
            g = curves[f]
            rel = case['release']
            peak_region = slice(case['attack']+int(.003*SR), rel+int(.02*SR))
            row['minimum_gain_db'] = float(np.min(20*np.log10(abs(g[peak_region, 0]))))
            # Diagnostic one-exponential fit to the recovery's complex deviation.
            a, b = rel+int(.004*SR), rel+int(.15*SR)
            tt = (np.arange(a, b)-rel)/SR
            target = g[a:b, 0]-1
            def objective(log_tau):
                basis = np.exp(-tt/np.exp(log_tau))
                amp = np.vdot(basis, target)/np.vdot(basis, basis)
                return np.linalg.norm(target-amp*basis)**2
            fit = optimize.minimize_scalar(objective, bounds=(np.log(.001), np.log(.5)), method='bounded')
            tau = np.exp(fit.x)
            basis = np.exp(-tt/tau)
            amp = np.vdot(basis, target)/np.vdot(basis, basis)
            row['single_exponential_tau_ms'] = float(tau*1000)
            row['single_exponential_error_relative_db'] = db(np.linalg.norm(target-amp*basis)/max(np.linalg.norm(target), 1e-30))
            rows.append(row)
            if case['duration'] == .1:
                a, b = case['attack']-int(.01*SR), case['release']+int(.18*SR)
                ax = axs[fi, 0 if case['frequency']==250 else 1]
                ax.plot((np.arange(a,b,48)-case['attack'])/SR*1000,
                        20*np.log10(abs(g[a:b:48, 0])), label=f"burst {db(case['amplitude']):.1f} dBFS peak")
    for fi, f in enumerate([1000, 8000]):
        for ci, cf in enumerate([250, 7000]):
            ax = axs[fi, ci]
            ax.axvline(100, color='black', alpha=.3, linestyle=':')
            ax.set(title=f'{f/1000:g} kHz probe / {cf/1000:g} kHz conditioner',
                   xlabel='Time from burst onset (ms)', ylabel='Incremental gain versus quiet state (dB)')
            ax.grid(alpha=.2)
            ax.legend(fontsize=8)
    fig.suptitle('P821 456 / 15 ips / Stage Full | 100 ms bursts | hiss and motion off')
    fig.savefig(ROOT/'dynamic-recovery.png', dpi=160)
    plt.close(fig)
    (ROOT/'envelope-analysis.json').write_text(json.dumps(rows, indent=2))
    # Store decimated complex trajectories for efficient model experiments.
    np.savez_compressed(ROOT/'envelope-trajectories.npz',
                        gain1000=curves[1000][::48], gain8000=curves[8000][::48])
    v = (read('conditioned-impulse-plus')-read('conditioned-impulse-minus'))*.5/.001
    baseline = linear_kernel()
    fig, ax = plt.subplots(figsize=(10, 5), layout='constrained')
    freq = np.geomspace(30, 20000, 240)
    n = 8192
    osc = np.exp(-2j*np.pi*freq[:, None]*np.arange(n)/SR)
    hb = osc @ baseline[0, 0, :n]
    impulse_rows = []
    for case in json.loads((ROOT/'conditioned-impulse-cases.json').read_text()):
        if case['channel'] != 0:
            continue
        a = case['impulse']
        hr = osc @ v[a:a+n, 0]
        ratio = hr / hb
        ax.semilogx(freq, 20*np.log10(abs(ratio)), label=f"gap {case['gap_ms']} ms")
        impulse_rows.append(case | {'first_sample_relative_db': db(abs(v[a, 0]/baseline[0, 0, 0])),
                                    'frequencies': freq.tolist(), 'gain_db': (20*np.log10(abs(ratio))).tolist(),
                                    'phase_degrees': np.angle(ratio, deg=True).tolist()})
    ax.set(title='Conditioned impulse response relative to quiet response',
           xlabel='Frequency (Hz)', ylabel='Relative gain (dB)', ylim=(-13, 7))
    ax.legend()
    ax.grid(alpha=.2)
    fig.savefig(ROOT/'conditioned-response.png', dpi=160)
    plt.close(fig)
    (ROOT/'conditioned-response.json').write_text(json.dumps(impulse_rows, indent=2))
    print(json.dumps([r for r in rows if r['duration']==.1 and r['amplitude']==.7], indent=2))


if __name__ == '__main__':
    main()
