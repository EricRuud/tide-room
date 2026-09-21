"""Capture harness using the installed plugin's normal VST3 processing API.

The target directory can be overridden by an importing experiment. No plugin
binary implementation is extracted. The host must use the same output directory.
"""
import json
import time
from pathlib import Path

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[2] / 'artifacts/p821-analysis-003'
SR = 48000
BASE = {'InfoDump':0, 'Bypass': 0, 'Polarity Flip': 0, 'Tape SW': 0, 'Formula SW': 0,
        'IPS SW': 0, 'Input': .5, 'Output': .5, 'Hiss': 0, 'Hiss Dim': 0,
        'Modulation SW': 0, 'Mod. Mode': 1, 'Wow': .1, 'Flutter': .1,
        'Flange SW': 0, 'DLY SW': 0, 'Lo-fi SW': 0,
        'LS SW': 1, 'HS SW': 1, 'LS Gain': .5, 'HS Gain': .5,
        'SHLF Pos SW': 0, 'Bias LO SW': 1, 'Bias HI SW': 1,
        'Bias LO Range': .5, 'Bias HI Range': .5,
        'Stage Focus': 0, 'Center SW': 0}


def submit(name, source, parameters=None, **options):
    result = ROOT / (name + '.json')
    if result.exists():
        report = json.loads(result.read_text())
        if not report.get('ok'): raise RuntimeError(report)
        return report
    output = ROOT / (name + '.wav')
    if output.exists(): raise RuntimeError('Uncompleted output already exists: '+str(output))
    job = {'id': name, 'input': str(source.resolve()), 'output': str(output),
           'parameters': BASE | (parameters or {}), 'warmup': 3, 'block': 256,
           'warmupInput': True, 'realtimeWarmup': True} | options
    mailbox = ROOT / 'command.json'
    queued = json.loads(mailbox.read_text()) if mailbox.exists() else None
    pending = queued and not (ROOT / (queued['id'] + '.json')).exists()
    if pending and queued['id'] != name:
        raise RuntimeError('Another measurement is queued: ' + queued['id'])
    if not pending:
        tmp = ROOT / 'command.tmp'
        tmp.write_text(json.dumps(job))
        tmp.replace(mailbox)
    start = time.monotonic()
    while not result.exists():
        if time.monotonic()-start > 300: raise TimeoutError(name)
        time.sleep(.1)
    report = json.loads(result.read_text())
    if not report.get('ok'): raise RuntimeError(report)
    print(name, 'CPU %.2f%%' % report.get('cpuPercent', 0), flush=True)
    return report


def save(name, data, rate=SR):
    path = ROOT / ('input-' + name + '.wav')
    if not path.exists(): sf.write(path, data, rate, subtype='FLOAT')
    return path


def build_tones(levels, modes, frequencies, name, rate=SR):
    segments, records = [], []
    n = int(rate*.75)
    t = np.arange(n)/rate
    for level in levels:
        for mode, channels in modes.items():
            for frequency in frequencies:
                mono = 10**(level/20)*np.sin(2*np.pi*frequency*t)
                data = mono[:,None]*np.asarray(channels)[None,:]
                start=len(segments)*n
                records.append(dict(start=start, end=start+n, frequency=frequency,
                                    level=level, mode=mode, channels=channels))
                segments.append(data)
    source=save(name,np.concatenate(segments),rate)
    source.with_suffix('.json').write_text(json.dumps(records,indent=2))
    return source


def main():
    t=np.arange(SR*120)/SR
    continuity=save('continuity',.1*np.sin(2*np.pi*1000*t)[:,None]*np.ones((1,2)))
    submit('demo-continuity',continuity)
    # Repeat the same source through bypass to validate host data movement.
    submit('host-bypass',ROOT.parent/'p821-analysis-001/smoke-input.wav',{'Bypass':1})
    freqs=[32,64,126,250,500,1000,2000,4000,8000,12000,16000]
    modes={'mono':[1,1], 'left':[1,0], 'right':[0,1], 'side':[1,-1], 'pan':[1,.5]}
    source=build_tones([-36],modes,freqs,'stereo-small')
    for tape in [1,0]:
        for focus in [0,.5,1]:
            for center in [0,1]:
                submit('stereo-%s-f%d-c%d'%('thru' if tape else 'tape',focus*2,center),source,
                       {'Tape SW':tape,'Stage Focus':focus,'Center SW':center})
    # Repeat baseline to measure residual variation with motion and hiss disabled.
    submit('stereo-tape-repeat',source,{'Stage Focus':1})
    nonlinear=build_tones([-30,-18,-6,0],{'mono':[1,1],'pan':[1,.5]},
                          [64,250,1000,4000,8000,12000,16000],'level-response')
    for formula in [0,1]:
        for speed in [0,1]:
            submit('levels-%d-%d'%(formula,speed),nonlinear,{'Formula SW':formula,'IPS SW':speed,'Stage Focus':1})
    submit('levels-focus-off',nonlinear)
    submit('levels-center-on',nonlinear,{'Stage Focus':1,'Center SW':1})
    submit('levels-thru',nonlinear,{'Stage Focus':1,'Tape SW':1})
    for level, knob in [('soft',0),('sharp',1)]:
        submit('bias-'+level,nonlinear,{'Bias HI Range':knob,'Stage Focus':1})
    # A coherent stationary tone makes new non-harmonic spectral components visible.
    # These include modulation/noise as well as aliasing; comparisons at another rate
    # are needed before attributing every spur to foldback.
    for rate in [44100,48000,96000]:
        signal=build_tones([-18,-6],{'mono':[1,1]},[1000,6000,11000,17000],
                           'spectral-%d'%rate,rate)
        for formula in [0,1]:
            submit('spectral-%d-%d'%(rate,formula),signal,{'Formula SW':formula,'Stage Focus':0})
    # Bursts and level steps retain attack/recovery context; ramp edges avoid clicks.
    t=np.arange(SR*8)/SR
    env=np.zeros(len(t))
    for start,duration,amp in [(1,.015,.7),(2,.05,.7),(3,.2,.7),(4,1,.7),(5,2,.025)]:
        a,b=int(start*SR),int((start+duration)*SR)
        env[a:b]=amp
        ramp=min(48,(b-a)//2)
        env[a:a+ramp]*=np.linspace(0,1,ramp)
        env[b-ramp:b]*=np.linspace(1,0,ramp)
    burst=save('bursts', (env*(np.sin(2*np.pi*250*t)+.3*np.sin(2*np.pi*8000*t)))[:,None]*[1,.6])
    for focus in [0,1]:
        submit('bursts-f%d'%focus,burst,{'Stage Focus':focus})
    motion=save('motion',(.1*np.sin(2*np.pi*3000*np.arange(SR*20)/SR))[:,None]*[1,1])
    for label, p in [('off',{}),('default',{'Modulation SW':1}),
                     ('wow',{'Modulation SW':1,'Wow':1,'Flutter':0}),
                     ('flutter',{'Modulation SW':1,'Wow':0,'Flutter':1})]:
        submit('motion-'+label,motion,p)
    silence=save('silence',np.zeros((SR*10,2)))
    submit('noise-off',silence)
    submit('noise-on',silence,{'Hiss':1})
    original=ROOT.parent/'machine-tape-001/listen/10-latest-room-untaped.wav'
    data,rate=sf.read(original,always_2d=True)
    # Retain the recorded spatial balance; trim to a useful audition duration.
    music=save('room',data[:int(rate*30)],rate)
    for label,p in [('focus-off',{}),('focus-half',{'Stage Focus':.5}),
                    ('focus-full',{'Stage Focus':1}),('center',{'Stage Focus':1,'Center SW':1}),
                    ('900',{'Stage Focus':1,'Formula SW':1}),
                    ('driven',{'Stage Focus':1,'Input':'9.0','Output':'-9.0'})]:
        submit('music-'+label,music,p)
    (ROOT/'session-complete.json').write_text(json.dumps({'complete':True}))


if __name__=='__main__': main()
