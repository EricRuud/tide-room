"""Independent stationary reference for the new 7:5 metallic source.

Construct the periodic waveform in continuous phase and retain only frequencies
below host Nyquist. No measured gain/phase fitting or high-rate runtime reference.
The envelope, moving gate and pitch transient are outside this stationary test.
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import special

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from lab import residual


def coefficients(index, folding, drive, size):
    theta = 2*np.pi*np.arange(size)/size
    folded = .35*(np.sin(folding*np.sin(5*theta)+.12)-np.sin(.12)*special.j0(folding))
    source = .72*.35*np.sin(5*theta+index*np.sin(7*theta))+.28*folded
    return np.fft.rfft(source-2*drive*source**3)/size


def analyze(folder):
    criteria = json.loads((ROOT/'criteria.json').read_text())
    band = criteria['measurement_band_fraction_of_sample_rate']
    limit = criteria['max_stationary_reference_residual_db']
    cache, rows = {}, []
    for path in sorted(folder.glob('*.wav')):
        sr, bin_number, index, folding, drive = map(float, path.stem.split('-'))
        actual, rate = sf.read(path)
        assert sr == rate and actual.shape == (8192,)
        key = index, folding, drive
        if key not in cache:
            c = coefficients(*key, 262144)
            assert np.max(np.abs(c[:2048]-coefficients(*key, 131072)[:2048])) < 1e-13
            cache[key] = c
        c = cache[key]
        bins = np.arange(int(np.ceil(4096/bin_number))) * int(bin_number)
        spectrum = np.zeros(4097, dtype=complex)
        spectrum[bins] = 8192*c[:len(bins)]*np.exp(2j*np.pi*bins*(4096-192)/8192)
        reference = np.fft.irfft(spectrum, n=8192)
        rows.append(dict(file=path.name, carrier_hz=sr*bin_number*5/8192,
                         index=index, folding=folding, drive=drive,
                         **residual(actual, reference, sr, band*sr)))
    assert len(rows) == 168
    worst = max(rows, key=lambda row: row['relative_db'])
    report = dict(passed=worst['relative_db'] <= limit, limit_db=limit,
                  measurement_band_fraction=band, worst=worst, cases=rows,
                  scope='Stationary metallic oscillator and saturation only; not a full-instrument alias guarantee.')
    (folder.parent/'metal-spectral-report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'cases'}, indent=2))
    return report['passed']


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('probes', type=Path)
    sys.exit(0 if analyze(parser.parse_args().probes) else 1)
