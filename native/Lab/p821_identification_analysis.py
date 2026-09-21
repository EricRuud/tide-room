"""Repeatability, linear baseline and controlled history-interaction diagnostics."""
import json
import io
import zipfile
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal

ROOT = Path(__file__).resolve().parents[2] / 'artifacts/p821-identification-001'
SR = 48000


def read(name):
    path=ROOT/(name+'.wav')
    if path.exists():
        x, sr = sf.read(path, always_2d=True)
    else:
        with zipfile.ZipFile(ROOT/'completed-diagnostics.zip') as archive:
            x, sr = sf.read(io.BytesIO(archive.read(path.name)),always_2d=True)
    assert sr == SR
    return x


def db(x):
    return float(20*np.log10(max(float(x), 1e-15)))


def metrics(y, prediction):
    e = prediction-y
    rms = np.sqrt(np.mean(y*y))
    gain = np.sum(y*prediction)/max(np.sum(prediction*prediction), 1e-30)
    return dict(residual_dbr=db(np.sqrt(np.mean(e*e))/max(rms, 1e-30)),
                residual_dbfs=db(np.sqrt(np.mean(e*e))),
                output_rms_dbfs=db(rms), peak_error=float(np.max(np.abs(e))),
                optimal_gain_db=db(abs(gain)),
                gain_aligned_residual_dbr=db(np.sqrt(np.mean((gain*prediction-y)**2))/max(rms, 1e-30)))


def linear_kernel(name='impulse-full', taps=32768):
    x, y = read('input-impulse'), read(name)
    h = np.empty((2, 2, taps))
    for i, start in enumerate([SR, int(3.5*SR)]):
        for o in range(2):
            h[o, i] = y[start:start+taps, o] / x[start, i]
    return h


def linear_render(x, h):
    y = np.zeros_like(x)
    for o in range(2):
        for i in range(2):
            y[:, o] += signal.fftconvolve(x[:, i], h[o, i])[:len(x)]
    return y


def main():
    report = {'repeatability': {}, 'linear': {}, 'history': []}
    for a, b in [('music-repeat-a', 'music-repeat-b'),
                 ('impulse-full', 'impulse-full-repeat'),
                 ('history-plus', 'history-plus-repeat')]:
        if (ROOT/(b+'.json')).exists():
            report['repeatability'][a] = metrics(read(a)[SR:], read(b)[SR:])
    h = linear_kernel()
    report['linear']['matrix_dc'] = h.sum(axis=2).tolist()
    report['linear']['matrix_first_sample'] = h[:, :, 0].tolist()
    report['linear']['symmetry_relative_db'] = db(np.linalg.norm(h[0, 0]-h[1, 1])/np.linalg.norm(h[0, 0]))
    for name, source in [('music-repeat-a', ROOT.parent/'p821-analysis-003/input-room.wav'),
                         ('music-levels', ROOT/'input-music-levels.wav'),
                         ('training-noise', ROOT/'input-training-noise.wav')]:
        if not (ROOT/(name+'.json')).exists():
            continue
        x, sr = sf.read(source, always_2d=True)
        assert sr == SR
        y = read(name)
        prediction = linear_render(x, h)
        report['linear'][name] = metrics(y[SR:], prediction[SR:])
        if source.with_suffix('.json').exists():
            report['linear'][name]['segments'] = [case | metrics(y[case['start']:case['end']], prediction[case['start']:case['end']])
                                                  for case in json.loads(source.with_suffix('.json').read_text())]
    if (ROOT/'history-plus-repeat.json').exists():
        c, p, plus, minus, rep = [read('history-'+k) for k in ['conditioner', 'probe', 'plus', 'minus', 'plus-repeat']]
        marginal = (plus-minus)*.5
        for case in json.loads((ROOT/'history-cases.json').read_text()):
            a, b = case['probe_start'], case['probe_end']+int(.05*SR)
            pp = p[a:b]
            interaction = plus[a:b]-c[a:b]-pp
            noise = rep[a:b]-plus[a:b]
            case['interaction_dbr'] = db(np.linalg.norm(interaction)/max(np.linalg.norm(pp), 1e-30))
            case['marginal_change_dbr'] = db(np.linalg.norm(marginal[a:b]-pp)/max(np.linalg.norm(pp), 1e-30))
            case['repeat_floor_dbr'] = db(np.linalg.norm(noise)/max(np.linalg.norm(pp), 1e-30))
            case['marginal_gain_db'] = db(abs(np.sum(marginal[a:b]*pp)/max(np.sum(pp*pp), 1e-30)))
            report['history'].append(case)
    (ROOT/'identification-analysis.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
