"""Stationary ring/AM/complex reference and tape spectral diagnostics.

Ring references integrate an independent continuous-phase formula, discard
out-of-band harmonics, and use the known FIR latency without fitting.
Tape alias scores exclude only physically in-band harmonics of a coherent sine;
32x renders are a numerical comparison, not an independent physical reference.
"""
from pathlib import Path
import argparse
import json
import sys
import numpy as np
import soundfile as sf
from scipy.special import j0

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from lab import residual

def ring_coefficients(mode,size):
    theta=2*np.pi*np.arange(size)/size
    folded=.35*(np.sin(6.4*np.sin(5*theta)+.12)-np.sin(.12)*j0(6.4))
    carrier=.8*.35*np.sin(5*theta)+.2*folded
    mod=np.sin(12*theta)
    if mode==4: x=carrier*(.04+.96*mod)
    elif mode==5: x=carrier*(1-.5*.96+.5*.96*mod)
    else: x=(.8*.35*np.sin(5*theta+2.4*mod)+.2*folded)*(1-.65*.96+.65*.96*mod)
    return np.fft.rfft(x-2*x**3)/size

def analyze(folder):
    rows=[];passed=True
    for path in sorted(folder.glob('ring-*.wav')):
        _,sr,k,mode=path.stem.split('-');sr,k,mode=map(int,(sr,k,mode))
        actual,rate=sf.read(path);assert rate==sr
        c=ring_coefficients(mode,262144);assert np.max(np.abs(c[:2048]-ring_coefficients(mode,131072)[:2048]))<1e-13
        bins=np.arange(int(np.ceil(4096/k)))*k
        spectrum=np.zeros(4097,complex);spectrum[bins]=8192*c[:len(bins)]*np.exp(2j*np.pi*bins*(4096-192)/8192)
        reference=np.fft.irfft(spectrum,n=8192)
        rows.append(dict(file=path.name,mode=mode,carrier_hz=sr*k*5/8192,**residual(actual,reference,sr,.4*sr)))
    if rows:
        assert len(rows)==36
        worst=max(rows,key=lambda r:r['relative_db']);report=dict(passed=worst['relative_db']<=-90,limit_db=-90,measurement_band='0 to 0.4 * sample rate',worst=worst,cases=rows)
        (folder.parent/'ring-spectral-report.json').write_text(json.dumps(report,indent=2)+'\n');print('RING',json.dumps(worst), 'PASS',report['passed']);passed &= report['passed']
    tape=[]
    for path in sorted(folder.glob('tape-*.wav')):
        _,sr,k,drive,order=path.stem.split('-');sr,k,drive,order=map(int,(sr,k,drive,order))
        actual,_=sf.read(path);x=actual[:,0];spec=np.fft.rfft(x);power=abs(spec)**2
        mask=np.ones(len(spec),bool);mask[0]=False;harmonics=np.arange(1,int(4096//k)+1)*k;mask[harmonics]=False
        band=np.arange(len(spec))<=int(.4*len(x));unwanted=np.sum(power[mask&band]);wanted=np.sum(power[~mask&band]);db=10*np.log10(max(unwanted,1e-30)/max(wanted,1e-30))
        tape.append(dict(file=path.name,rate=sr,frequency=sr*k/8192,drive_db=drive,oversampling=1<<order,unwanted_relative_db=db,peak=float(abs(x).max())))
    if tape:
        worst=max((r for r in tape if r['oversampling']==32),key=lambda r:r['unwanted_relative_db'])
        assert len(tape)==60
        report=dict(passed=bool(worst['unwanted_relative_db']<=-90),limit_db=-90,worst_32x=worst,scope='Non-harmonic spectral energy in 0–0.4 Fs for stationary single tones, including numerical/filter error. Not an all-signal alias guarantee.',cases=tape)
        (folder.parent/'tape-spectral-report.json').write_text(json.dumps(report,indent=2)+'\n');print('TAPE',json.dumps(worst));passed &= report['passed']
    return passed

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path)
    sys.exit(0 if analyze(parser.parse_args().folder) else 1)
