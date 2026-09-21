"""Independent stationary Fourier reference for the compiled nonlinear oscillator.

Unlike the runtime, this constructs harmonics in continuous phase and explicitly
omits those at/above Nyquist. No fitted phase, gain, or error-dependent alignment.
Includes DC, float/24-bit quantization and passband filtering error, not alias alone.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import numpy as np
from scipy import special
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from lab import residual


def coefficients(drive, saturation, n=262144):
    phase = 2*np.pi*np.arange(n)/n
    raw = .35*(np.sin(drive*np.sin(phase)+.12)-np.sin(.12)*special.j0(drive))
    return np.fft.rfft(raw-2*saturation*raw**3)/n


def analyze(folder):
    criteria = json.loads((ROOT/'criteria.json').read_text())
    band = criteria['measurement_band_fraction_of_sample_rate']
    limit = criteria['max_stationary_reference_residual_db']
    assert 0 < band <= .4, 'Measurement must stay within the validated FIR passband'
    rows, cache = [], {}
    for path in sorted(folder.glob('*.wav')):
        sr, bin_number, drive, saturation = map(float, path.stem.split('-'))
        audio, actual_sr = sf.read(path)
        assert sr == actual_sr and audio.shape == (8192,)
        key = drive, saturation
        if key not in cache:
            c = coefficients(*key)
            c2 = coefficients(*key, n=131072)
            assert np.max(np.abs(c[:2048]-c2[:2048])) < 1e-13
            cache[key] = c
        c = cache[key]
        f = sr*bin_number/len(audio)
        phase = 2*np.pi*f*(np.arange(len(audio))+4096-192)/sr
        ref = np.full(len(audio), c[0].real)
        for h in range(1, int(np.ceil(sr/(2*f)))):
            ref += 2*np.real(c[h]*np.exp(1j*h*phase))
        error = residual(audio, ref, sr, band*sr)
        rows.append(dict(file=path.name, sample_rate=sr, frequency_hz=f,
                         folding=drive, saturation=saturation, **error))
    assert len(rows) == 168, f'Expected 168 cases, found {len(rows)}'
    worst = max(rows, key=lambda x:x['relative_db'])
    report = dict(measurement=f'Relative RMS residual through {band} x sample rate',
                  method='Independent continuous-phase Fourier coefficients; fixed 192-frame latency; no fitted alignment/gain',
                  limit_db=limit, passed=worst['relative_db'] <= limit, worst=worst, cases=rows,
                  scope='Stationary oscillator and per-voice saturation; not a guarantee for arbitrary automation, polyphony, or all frequencies',
                  source_hashes={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
                                 for p in [*(ROOT/'native/Source').glob('*'),ROOT/'native/analyze.py',ROOT/'criteria.json']})
    (folder.parent/'spectral-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('cases','source_hashes')},indent=2))
    return report['passed']


if __name__ == '__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('probes',type=Path)
    sys.exit(0 if analyze(parser.parse_args().probes) else 1)
