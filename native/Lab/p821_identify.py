"""Controlled P821 behavior-identification experiments, isolated from Tide Room.

Audio I/O uses the installed demo through its normal VST3 API. No binary analysis.
The user was informed of the manual's replication/reverse-engineering restriction.
"""
import argparse
import json
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal

import p821_measure as bench

ROOT = Path(__file__).resolve().parents[2] / 'artifacts/p821-identification-001'
PREVIOUS = ROOT.parent / 'p821-analysis-003'
SR = 48000
bench.ROOT = ROOT
CONFIG = {'Stage Focus': 1}


def save(name, audio):
    path = ROOT / ('input-' + name + '.wav')
    if not path.exists():
        sf.write(path, np.asarray(audio), SR, subtype='FLOAT')
    return path


def submit(name, source, parameters=None, **options):
    return bench.submit(name, source, CONFIG | (parameters or {}), **options)


def initial():
    ROOT.mkdir(parents=True, exist_ok=True)
    music = PREVIOUS / 'input-room.wav'
    submit('music-repeat-a', music)
    submit('music-repeat-b', music)
    impulse = np.zeros((6 * SR, 2))
    impulse[SR, 0] = .001
    impulse[int(3.5 * SR), 1] = .001
    source = save('impulse', impulse)
    submit('impulse-full', source)
    submit('impulse-off', source, {'Stage Focus': 0})
    submit('impulse-center', source, {'Center SW': 1})
    submit('impulse-full-repeat', source)
    # Holdout input spans operating levels, preserving the original waveform.
    x, sr = sf.read(music, always_2d=True)
    assert sr == SR
    blocks = []
    records = []
    for gain in [0, 12, 18]:
        a = sum(len(b) for b in blocks)
        blocks += [np.zeros((SR, 2)), x * 10 ** (gain / 20), np.zeros((SR, 2))]
        records.append({'gain_db': gain, 'start': a + SR, 'end': a + SR + len(x)})
    source = save('music-levels', np.concatenate(blocks))
    source.with_suffix('.json').write_text(json.dumps(records, indent=2))
    submit('music-levels', source)


def history():
    ROOT.mkdir(parents=True, exist_ok=True)
    cases = []
    conditioners, probes = [], []
    for cf, pf in [(250, 1000), (250, 8000), (7000, 1000), (7000, 8000)]:
        for gap_ms in [0, .5, 2, 8, 32, 128, 512]:
            c = np.zeros((2 * SR, 2))
            p = np.zeros_like(c)
            start, end = int(.4 * SR), int(.5 * SR)
            t = np.arange(end - start) / SR
            ramp = 24
            env = np.ones(len(t))
            env[:ramp] = np.linspace(0, 1, ramp)
            env[-ramp:] = np.linspace(1, 0, ramp)
            c[start:end] = (.7 * env * np.sin(2 * np.pi * cf * t))[:, None] * [1, .6]
            pa = end + round(gap_ms * SR / 1000)
            pn = int(.05 * SR)
            env = np.ones(pn)
            env[:ramp] = np.linspace(0, 1, ramp)
            env[-ramp:] = np.linspace(1, 0, ramp)
            p[pa:pa+pn] = (.005 * env * np.sin(2 * np.pi * pf * np.arange(pn) / SR))[:, None] * [1, .6]
            offset = len(cases) * len(c)
            cases.append(dict(conditioner_hz=cf, probe_hz=pf, gap_ms=gap_ms,
                              start=offset, conditioner_start=offset+start,
                              conditioner_end=offset+end, probe_start=offset+pa,
                              probe_end=offset+pa+pn, end=offset+len(c)))
            conditioners.append(c)
            probes.append(p)
    c, p = np.concatenate(conditioners), np.concatenate(probes)
    (ROOT / 'history-cases.json').write_text(json.dumps(cases, indent=2))
    for name, x in [('conditioner', c), ('probe', p), ('plus', c+p), ('minus', c-p)]:
        submit('history-' + name, save('history-' + name, x))
    submit('history-plus-repeat', ROOT / 'input-history-plus.wav')


def training():
    ROOT.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(821)
    blocks, cases = [], []
    for level in [-36, -24, -12, -3]:
        n = 4 * SR
        x = rng.normal(size=(n, 2))
        x = signal.sosfilt(signal.butter(2, 17000, fs=SR, output='sos'), x, axis=0)
        # A broad spectrum, varied envelope and independent channels excite
        # interactions. Peak is controlled; no reference output is normalized.
        t = np.arange(n) / SR
        env = .15 + .85 * (.5 + .5 * np.sin(2*np.pi*.7*t)) ** 2
        x *= env[:, None]
        x *= 10 ** (level / 20) / np.max(np.abs(x))
        a = sum(len(b) for b in blocks)
        blocks += [np.zeros((SR, 2)), x, np.zeros((SR, 2))]
        cases.append(dict(level_peak_dbfs=level, start=a+SR, end=a+SR+n))
    source = save('training-noise', np.concatenate(blocks))
    source.with_suffix('.json').write_text(json.dumps(cases, indent=2))
    submit('training-noise', source)


def envelopes():
    """Small-signal response while a separate carrier drives the operating point."""
    ROOT.mkdir(parents=True, exist_ok=True)
    blocks, cases = [], []
    n = 2 * SR
    for frequency in [250, 7000]:
        for amplitude in [.03, .1, .3, .7]:
            for duration in [.005, .02, .1, .5]:
                c = np.zeros((n, 2))
                a, b = int(.5*SR), int((.5+duration)*SR)
                env = np.ones(b-a)
                env[:24] = np.linspace(0, 1, 24)
                env[-24:] = np.linspace(1, 0, 24)
                c[a:b] = (amplitude * env * np.sin(2*np.pi*frequency*np.arange(b-a)/SR))[:, None] * [1, .6]
                offset = len(blocks) * n
                cases.append(dict(frequency=frequency, amplitude=amplitude, duration=duration,
                                  start=offset, attack=offset+a, release=offset+b, end=offset+n))
                blocks.append(c)
    c = np.concatenate(blocks)
    t = np.arange(len(c))/SR
    p = (.001 * (np.sin(2*np.pi*1000*t) + np.sin(2*np.pi*8000*t)))[:, None] * [1, .6]
    (ROOT/'envelope-cases.json').write_text(json.dumps(cases, indent=2))
    submit('envelope-plus', save('envelope-plus', c+p))
    submit('envelope-minus', save('envelope-minus', c-p))
    submit('envelope-probe', save('envelope-probe', p))
    # Conditioned impulses sample the local response at different history states.
    cases, cs, ps = [], [], []
    for gap_ms in [0, 2, 8, 32, 128, 512]:
        for channel in [0, 1]:
            c, p = np.zeros((2*SR, 2)), np.zeros((2*SR, 2))
            a, b = int(.4*SR), int(.5*SR)
            env = np.ones(b-a)
            env[:24] = np.linspace(0, 1, 24)
            env[-24:] = np.linspace(1, 0, 24)
            c[a:b] = (.7*env*np.sin(2*np.pi*250*np.arange(b-a)/SR))[:, None]*[1, .6]
            pa = b+round(gap_ms*SR/1000)
            p[pa, channel] = .001
            offset = len(cs)*len(c)
            cases.append(dict(gap_ms=gap_ms, channel=channel, start=offset,
                              impulse=offset+pa, end=offset+len(c)))
            cs.append(c)
            ps.append(p)
    c, p = np.concatenate(cs), np.concatenate(ps)
    (ROOT/'conditioned-impulse-cases.json').write_text(json.dumps(cases, indent=2))
    submit('conditioned-impulse-plus', save('conditioned-impulse-plus', c+p))
    submit('conditioned-impulse-minus', save('conditioned-impulse-minus', c-p))


def routing():
    ROOT.mkdir(parents=True, exist_ok=True)
    cs, ps, cases = [], [], []
    for cf in [250, 7000]:
        for pc in [0, 1]:
            c, p = np.zeros((2*SR, 2)), np.zeros((2*SR, 2))
            a, b = int(.5*SR), SR
            env = np.ones(b-a)
            env[:24] = np.linspace(0, 1, 24)
            env[-24:] = np.linspace(1, 0, 24)
            c[a:b, 0] = .7*env*np.sin(2*np.pi*cf*np.arange(b-a)/SR)
            p[:, pc] = .001*np.sin(2*np.pi*8000*np.arange(len(p))/SR)
            offset = len(cs)*len(c)
            cases.append(dict(conditioner_hz=cf, probe_channel=pc, start=offset,
                              attack=offset+a, release=offset+b, end=offset+len(c)))
            cs.append(c)
            ps.append(p)
    c, p = np.concatenate(cs), np.concatenate(ps)
    (ROOT/'routing-cases.json').write_text(json.dumps(cases, indent=2))
    submit('routing-plus', save('routing-plus', c+p))
    submit('routing-minus', save('routing-minus', c-p))
    submit('routing-probe', save('routing-probe', p))
    source = ROOT/'input-history-plus.wav'
    submit('history-block64', source, block=64)
    submit('history-block1024', source, block=1024)


def blocks():
    for name in ['plus', 'minus']:
        submit('envelope-block1024-'+name, ROOT/('input-envelope-'+name+'.wav'), block=1024, warmup=4.096)
    # A smaller excerpt gives a fast repeat of the block-size finding with
    # identical warmup duration at both sizes.
    x, sr = sf.read(ROOT/'input-history-plus.wav', always_2d=True)
    source = save('block-control', x[:4*SR])
    submit('block-control-256', source, block=256, warmup=4.096)
    submit('block-control-1024', source, block=1024, warmup=4.096)


def music_training():
    rng = np.random.default_rng(1821)
    blocks = []
    for level in [.04, .15, .5, 1.1, .3, .8]:
        n = 8*SR
        x = np.zeros((n, 2))
        for event in range(48):
            start = int(rng.uniform(0, 7)*SR)
            duration = min(int(rng.uniform(.15, 1.2)*SR), n-start)
            t = np.arange(duration)/SR
            f = 55*2**(rng.integers(0, 49)/12)
            attack = rng.uniform(.0005, .02)
            release = rng.uniform(.03, .5)
            env = (1-np.exp(-t/attack))*np.exp(-t/release)
            voice = np.zeros(duration)
            for harmonic in range(1, 12):
                hf = f*harmonic
                if hf < 19000:
                    voice += np.sin(2*np.pi*hf*t+rng.uniform(-np.pi, np.pi))/harmonic**rng.uniform(.8, 2.0)
            if event % 3 == 0:
                voice *= np.sin(2*np.pi*f*1.41421356*t)
            pan = rng.uniform(.05, .95)
            x[start:start+duration] += (env*voice*rng.uniform(.1, .7))[:, None]*[np.cos(pan*np.pi/2), np.sin(pan*np.pi/2)]
        for delay, gain in [(.037, .2), (.061, -.1), (.089, .15)]:
            shift = int(delay*SR)
            x[shift:] += gain*x[:-shift, ::-1]
        x *= level / max(np.max(abs(x)), 1e-12)
        blocks += [np.zeros((SR, 2)), x, np.zeros((SR, 2))]
    source = save('training-patterns', np.concatenate(blocks))
    submit('training-patterns', source)


def final_holdouts():
    """Unseen waveforms reserved for evaluation after model selection."""
    directory=ROOT.parent/'machine-tape-001/reference/akai/results/EXP2_RealData/MAXELL_IPS[7.5]/predictions'
    manifest=[]
    for name,paths in [('test-research-mono',[directory/'52_input.wav']),
                       ('test-research-stereo',[directory/'98_input.wav',directory/'109_input.wav']),
                       ('test-tide-reeds',[ROOT.parent/'quality-audit-001/probes/reeds-attack-2ms.wav'])]:
        tracks=[]
        for path in paths:
            v,sr=sf.read(path,always_2d=True)
            if sr!=SR:
                from math import gcd
                divisor=gcd(sr,SR)
                v=signal.resample_poly(v,SR//divisor,sr//divisor,axis=0)
            tracks.append(v)
        if len(tracks)==2:
            n=min(len(v) for v in tracks)
            x=np.column_stack([tracks[0][:n,0],tracks[1][:n,0]])
        else:
            x=tracks[0]
            if x.shape[1]==1:x=np.repeat(x,2,axis=1)
        gain=min(.12/max(np.sqrt(np.mean(x*x)),1e-12),.9/max(np.max(abs(x)),1e-12))
        x*=gain
        source=save(name,np.concatenate([np.zeros((SR,2)),x,np.zeros((SR,2))]))
        manifest.append(dict(name=name,sources=[str(p) for p in paths],gain=gain,
                             active_start=SR,active_end=SR+len(x),sample_rate=SR))
        submit(name,source)
    (ROOT/'final-holdout-manifest.json').write_text(json.dumps(manifest,indent=2))


def causality():
    # Identical warmup and a long identical prefix. A future input change can
    # reveal whole-buffer control updates, without guessing from block-size tests.
    n=22*SR
    t=np.arange(n)/SR
    reference=.02*np.sin(2*np.pi*997*t)[:,None]*np.array([[1.,.6]])
    changed=reference.copy()
    cases=[]
    for j,offset in enumerate([16,64,128,240]):
        block_start=((6+j*4)*SR//256)*256
        a=block_start+offset
        length=round(.1*SR)
        env=np.ones(length)
        env[:16]=np.linspace(0,1,16)
        env[-16:]=np.linspace(1,0,16)
        changed[a:a+length]+=(.7*env*np.sin(2*np.pi*250*np.arange(length)/SR))[:,None]*[1.,.6]
        cases.append(dict(block_start=block_start,event=a,offset=offset,end=a+length))
    (ROOT/'causality-cases.json').write_text(json.dumps(cases,indent=2))
    submit('causality-reference',save('causality-reference',reference))
    source=save('causality-changed',changed)
    submit('causality-changed',source)
    submit('causality-repeat',source)


def stereo_structure():
    rng=np.random.default_rng(11821)
    t=np.arange(4*SR)/SR
    carrier=sum(np.sin(2*np.pi*f*t+rng.uniform(-np.pi,np.pi))/np.sqrt(f/89)
                for f in [89,127,181,257,367,523,743,1057,1501,2131,3037])
    carrier*=.6+.4*np.sin(2*np.pi*.73*t)**2
    carrier/=np.max(abs(carrier))
    mono=np.concatenate([np.zeros(3*SR)]+[carrier*a for a in [.05,.2,.7]]+[np.zeros(2*SR)])
    for label,amount in [('left',0),('mono',1),('side',-1),('pan',.6),('anti-pan',-.6)]:
        submit('structure-'+label,save('structure-'+label,mono[:,None]*[1.,amount]))
    submit('structure-mono-repeat',ROOT/'input-structure-mono.wav')


def quality_checks():
    parts=[np.zeros((3*SR,2))]
    cases=[]
    offset=3*SR
    for peak in [.02,.2,.7]:
        for frequency in [1000,8000,17000]:
            n=round(1.5*SR)
            env=np.ones(n)
            env[:64]=np.linspace(0,1,64)
            env[-64:]=np.linspace(1,0,64)
            tone=peak*env*np.sin(2*np.pi*frequency*np.arange(n)/SR)
            parts.append(np.repeat(tone[:,None],2,axis=1))
            cases.append(dict(peak=peak,frequency=frequency,start=offset,end=offset+n))
            offset+=n
    (ROOT/'quality-tone-cases.json').write_text(json.dumps(cases,indent=2))
    submit('quality-tones',save('quality-tones',np.concatenate(parts)))
    x=np.zeros((14*SR,2))
    x[3*SR:5*SR]=.1
    x[8*SR:10*SR]=.7
    submit('quality-dc',save('quality-dc',x))
    submit('quality-silence',save('quality-silence',np.zeros((3*SR,2))))


def training_grid():
    rng=np.random.default_rng(15821)
    cases=[];parts=[np.zeros((3*SR,2))];offset=3*SR
    for peak in [.05,.15,.4,.7,.95]:
        for frequency in [63,137,293,631,1357,2917,6271,13483]:
            for mode,pan in [('mono',[1,1]),('pan',[1,.4]),('side',[1,-1])]:
                t=np.arange(SR)/SR
                env=np.ones(SR);env[:48]=np.linspace(0,1,48);env[-48:]=np.linspace(1,0,48)
                tone=peak*env*np.sin(2*np.pi*frequency*t+rng.uniform(-np.pi,np.pi))
                parts+=[tone[:,None]*pan,np.zeros((SR//2,2))]
                cases.append(dict(start=offset,end=offset+SR,peak=peak,frequency=frequency,mode=mode))
                offset+=SR+SR//2
    (ROOT/'training-grid-cases.json').write_text(json.dumps(cases,indent=2))
    submit('training-grid',save('training-grid',np.concatenate(parts)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('stage', choices=['initial', 'history', 'training', 'envelopes', 'routing', 'blocks', 'music_training', 'final_holdouts', 'causality', 'stereo_structure', 'quality_checks', 'training_grid', 'all'])
    args = parser.parse_args()
    for stage in ['initial', 'history', 'training', 'envelopes', 'routing', 'blocks', 'music_training', 'final_holdouts', 'causality', 'stereo_structure', 'quality_checks', 'training_grid']:
        if args.stage in [stage, 'all']:
            globals()[stage]()
