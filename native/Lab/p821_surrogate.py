"""Experimental state-conditioned filter model of one fixed P821 configuration.

Separate offline research model. Coefficients depend on multiscale band energy.
The model uses 256-sample analysis frames; it is not a production real-time effect.
"""
import argparse
import json
import time
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal
import torch
from torch import nn

from p821_identification_analysis import ROOT, SR, linear_kernel, linear_render, metrics

BLOCK = 256
FREQUENCIES = [40, 100, 250, 600, 1500, 3500, 8000, 16000]
VERSION = 'v1'


def basis_and_control(x, h, nonlinear=True):
    # The measured low-level stereo matrix is factored out of the linear kernel.
    w = h[:, :, 0] / (h[0, 0, 0]+h[0, 1, 0])
    base = linear_render(x, h) @ np.linalg.inv(w).T
    sources = [base]
    if nonlinear:
        sources.append(base**3)
    basis = []
    for source in sources:
        basis.append(source.astype(np.float32))
        for f in FREQUENCIES:
            basis.append(signal.sosfilt(signal.butter(2, f, fs=SR, output='sos'), source, axis=0).astype(np.float32))
            b, a = signal.iirpeak(f, .7, fs=SR)
            basis.append(signal.lfilter(b, a, source, axis=0).astype(np.float32))
        if VERSION != 'v1':
            for f, q in [(22, .7), (65, 6), (135, .8), (180, 2), (250, 2), (350, 2),
                         (537, 1), (750, 2), (1100, 2), (1815, 1.1), (2600, 2),
                         (4000, 2), (6000, 2), (10000, 2), (14000, 2)]:
                b, a = signal.iirpeak(f, q, fs=SR)
                basis.append(signal.lfilter(b, a, source, axis=0).astype(np.float32))
    audio = np.stack(basis, axis=2)
    control, weight = control_features(x)
    return base, audio, control, weight, w


def control_features(x):
    specifications = [None, (300,'lowpass'), (3000,'highpass')]
    if VERSION != 'v1':
        for lo, hi in [(30, 120), (120, 450), (450, 1500), (1500, 5000), (5000, 16000)]:
            specifications.append(([lo,hi],'bandpass'))
    padded = int(np.ceil(len(x)/BLOCK))*BLOCK
    control = []
    for spec in specifications:
        band=x if spec is None else signal.sosfilt(signal.butter(2,spec[0],btype=spec[1],fs=SR,output='sos'),x,axis=0)
        bp = np.pad(band, ((0, padded-len(x)), (0, 0)))
        power = np.mean(bp.reshape(-1, BLOCK, 2)**2, axis=1)
        for tau in [0, .005, .02, .08, .3, 1.0]:
            if tau:
                alpha = np.exp(-BLOCK/(SR*tau))
                e = signal.lfilter([1-alpha], [1, -alpha], power, axis=0)
            else:
                e = power
            control.append(np.clip(np.log10(np.maximum(e, 1e-12))/6+1, -1, 1))
    peak = np.max(abs(np.pad(x, ((0, padded-len(x)), (0, 0))).reshape(-1, BLOCK, 2)), axis=1)
    control.append(np.clip(np.log10(np.maximum(peak, 1e-6))/3+1, -1, 1))
    own = np.stack(control, axis=2)
    control = np.concatenate([own, own[:, ::-1]], axis=2).astype(np.float32)
    energy = signal.lfilter([1-np.exp(-1/(SR*.02))], [1, -np.exp(-1/(SR*.02))], x*x, axis=0)
    weight = 1/np.sqrt(.003**2 + energy)
    return control, weight


class Model(nn.Module):
    def __init__(self, controls, basis):
        super().__init__()
        self.network = nn.Sequential(nn.Linear(controls, 64), nn.Tanh(),
                                     nn.Linear(64, 64), nn.Tanh(), nn.Linear(64, basis))
        nn.init.zeros_(self.network[-1].weight)
        nn.init.zeros_(self.network[-1].bias)

    def forward(self, previous, current, fraction, basis):
        coefficients = self.network(previous)*(1-fraction[:, None]) + self.network(current)*fraction[:, None]
        return torch.sum(coefficients*basis, dim=1)


def data(name, stride=8):
    report = json.loads((ROOT/(name+'.json')).read_text())
    x, sr = sf.read(report['job']['input'], always_2d=True)
    y, ysr = sf.read(report['job']['output'], always_2d=True)
    assert sr == ysr == SR and x.shape == y.shape
    h = linear_kernel()
    base, basis, control, weight, w = basis_and_control(x, h)
    y = y @ np.linalg.inv(w).T
    pos = np.arange(SR, len(x), stride)
    # Interleave the two independent output-channel observations. Include the
    # other channel's energy in controls so stereo-linked behavior is learnable.
    bs = basis[pos].reshape(-1, basis.shape[-1])
    ys = (y-base)[pos].reshape(-1)
    weights = weight[pos].reshape(-1)
    control = np.concatenate([np.zeros_like(control[:1]), control], axis=0)
    ci = np.stack([(pos//BLOCK+1)*2, (pos//BLOCK+1)*2+1], axis=1).reshape(-1)
    fraction = np.repeat((pos % BLOCK+1)/BLOCK, 2).astype(np.float32)
    return dict(name=name, control=torch.from_numpy(control.reshape(-1, control.shape[-1])),
                basis=torch.from_numpy(bs), target=torch.from_numpy(ys.astype(np.float32)),
                weight=torch.from_numpy(weights.astype(np.float32)),
                indices=torch.from_numpy(ci), fraction=torch.from_numpy(fraction),
                y=y[pos].reshape(-1), base=base[pos].reshape(-1))


@torch.no_grad()
def evaluate(model, d):
    predictions = []
    for a in range(0, len(d['basis']), 32768):
        b = min(a+32768, len(d['basis']))
        ci = d['indices'][a:b]
        predictions.append(model(d['control'][ci-2], d['control'][ci], d['fraction'][a:b], d['basis'][a:b]).numpy())
    predicted = d['base']+np.concatenate(predictions)
    return metrics(d['y'], predicted)


def train(steps=1600):
    torch.set_num_threads(3)
    torch.manual_seed(821)
    np.random.seed(821)
    train = [data('training-noise'), data('envelope-plus')]
    if VERSION != 'v1':
        train.append(data('training-patterns'))
    validation = [data('music-levels'), data('history-plus')]
    print('Prepared', [len(d['basis']) for d in train], flush=True)
    model = Model(train[0]['control'].shape[-1], train[0]['basis'].shape[-1])
    optimizer = torch.optim.Adam(model.parameters(), lr=.001)
    log = []
    start = time.monotonic()
    for step in range(steps+1):
        if step % 200 == 0:
            row = {'step': step, 'seconds': time.monotonic()-start,
                   'training': {d['name']: evaluate(model, d) for d in train},
                   'validation': {d['name']: evaluate(model, d) for d in validation}}
            log.append(row)
            (ROOT/f'surrogate-{VERSION}-training.json').write_text(json.dumps(log, indent=2))
            torch.save({'state_dict': model.state_dict(), 'controls': train[0]['control'].shape[-1],
                        'basis': train[0]['basis'].shape[-1], 'step': step,
                        'config': 'P821 1.5 / 456 / 15 ips / Stage Full / Center off / noise and motion off / zero trims',
                        'analysis_block': BLOCK}, ROOT/f'surrogate-{VERSION}.pt')
            print(step, 'train', {k: round(v['residual_dbr'], 2) for k,v in row['training'].items()},
                  'validation', {k: round(v['residual_dbr'], 2) for k,v in row['validation'].items()}, flush=True)
        if step == steps:
            break
        d = train[step % len(train)]
        ix = torch.randint(len(d['basis']), (8192,))
        ci = d['indices'][ix]
        predicted = model(d['control'][ci-2], d['control'][ci], d['fraction'][ix], d['basis'][ix])
        loss = torch.mean(((predicted-d['target'][ix])*d['weight'][ix])**2)
        optimizer.zero_grad()
        loss.backward()
        nn.utils.clip_grad_norm_(model.parameters(), 1)
        optimizer.step()


def render(source, destination):
    torch.set_num_threads(3)
    checkpoint = torch.load(ROOT/f'surrogate-{VERSION}.pt', weights_only=True)
    model = Model(checkpoint['controls'], checkpoint['basis'])
    model.load_state_dict(checkpoint['state_dict'])
    model.eval()
    x, sr = sf.read(source, always_2d=True)
    assert sr == SR
    h = linear_kernel()
    base, basis, control, weight, w = basis_and_control(x, h)
    control = np.concatenate([np.zeros_like(control[:1]), control], axis=0)
    with torch.no_grad():
        coefficients = model.network(torch.from_numpy(control)).numpy()
    pos = np.arange(len(x))
    fraction = ((pos % BLOCK+1)/BLOCK).astype(np.float32)
    indices = pos//BLOCK
    # Process in moderate chunks to avoid a full audio-rate coefficient tensor.
    output = base.copy()
    for a in range(0, len(x), 32768):
        b = min(a+32768, len(x))
        f = fraction[a:b, None, None]
        co = coefficients[indices[a:b]]*(1-f)+coefficients[indices[a:b]+1]*f
        output[a:b] += np.sum(basis[a:b]*co, axis=2)
    output = output @ w.T
    assert np.isfinite(output).all()
    sf.write(destination, output, sr, subtype='FLOAT')
    print(destination, float(np.max(abs(output))), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('mode', choices=['train', 'render'])
    parser.add_argument('--steps', type=int, default=1600)
    parser.add_argument('--version', choices=['v1', 'v2'], default='v1')
    parser.add_argument('--input', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    VERSION = args.version
    if args.mode == 'train':
        train(args.steps)
    else:
        render(args.input, args.output)
