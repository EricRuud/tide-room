"""One pilot at a time around a periodic bass carrier; no model fitting.

Includes a half-amplitude convergence check and a repeated identical capture.
The same-frequency response is only one part of a periodic nonlinear system's
small-signal behavior: output at translated frequencies is measured separately.
"""
import argparse,json
import numpy as np
import soundfile as sf
from p821_lf_experiments import ROOT,SR,N,BLOCK,shape,save,submit,save_json
from p821_identification_analysis import metrics


def capture():
    cases=[];n=896*BLOCK;a=256*BLOCK;b=448*BLOCK;t=np.arange(n)/N
    carrier=np.zeros(n);carrier[a:b]=.4*np.sin(2*np.pi*8*t[a:b]);carrier[a:a+24]*=np.linspace(0,1,24);carrier[b-24:b]*=np.linspace(1,0,24)
    for fbin,scale in [(b,1.) for b in [3,7,9,11,17,23,61,173]]+[(7,.5)]:
        name=f'single-pilot-{fbin:03d}-'+('full' if scale==1 else 'half');pilot=.00015*scale*np.cos(2*np.pi*fbin*t+.317)
        cases.append(dict(name=name,pilot_bin=fbin,pilot_amplitude=.00015*scale,carrier_bin=8,carrier_peak=.4,attack=a,release=b,length=n))
        save_json(ROOT/'single-pilot-cases.json',cases)
        for sign,label in [(1,'plus'),(-1,'minus')]:submit(name+'-'+label,save(name+'-'+label,(carrier+sign*pilot)[:,None]*[1,1]))
        if fbin==7 and scale==1:submit(name+'-repeat',ROOT/f'input-{name}-plus.wav')


def analyze():
    cases=json.loads((ROOT/'single-pilot-cases.json').read_text());assert len(cases)==9;rows=[];marginals={}
    for case in cases:
        yp,_=sf.read(ROOT/(case['name']+'-plus.wav'),always_2d=True);ym,_=sf.read(ROOT/(case['name']+'-minus.wav'),always_2d=True)
        xp,_=sf.read(ROOT/('input-'+case['name']+'-plus.wav'),always_2d=True);xm,_=sf.read(ROOT/('input-'+case['name']+'-minus.wav'),always_2d=True)
        y=(yp-ym)*.5;x=(xp-xm)*.5;marginals[case['name']]=y;periods=[]
        for start in range(0,len(y)-N+1,N):
            Y=np.fft.rfft(y[start:start+N,0]);X=np.fft.rfft(x[start:start+N,0]);k=case['pilot_bin'];H=Y[k]/X[k]
            translated=sorted({abs(k+2*j*case['carrier_bin']) for j in range(-5,6)}-{k,0})
            periods.append(dict(start=start,gain_db=float(20*np.log10(abs(H))),phase_deg=float(np.angle(H,deg=True)),
                translated_frequencies_hz=[j*SR/N for j in translated],translated_amplitude_vs_input_db=[float(20*np.log10(max(abs(Y[j]/X[k]),1e-15))) for j in translated]))
        rows.append(case|dict(periods=periods));plateau=periods[11:14]
        print(case['name'],'plateau gain/phase',np.mean([p['gain_db'] for p in plateau]),np.mean([p['phase_deg'] for p in plateau]),flush=True)
    full=marginals['single-pilot-007-full'];half=2*marginals['single-pilot-007-half'];repeat,_=sf.read(ROOT/'single-pilot-007-full-repeat.wav',always_2d=True);original,_=sf.read(ROOT/'single-pilot-007-full-plus.wav',always_2d=True)
    a=11*N;b=14*N;convergence=metrics(full[a:b],half[a:b]);noise=metrics(full[a:b],full[a:b]+(repeat[a:b]-original[a:b])*.5)
    save_json(ROOT/'single-pilot-analysis.json',dict(rows=rows,half_amplitude_convergence=convergence,repeat_noise_relative_to_marginal=noise,no_fitting=True,final_used=False))
    print('Half amplitude convergence',convergence['residual_dbr'],'repeat floor',noise['residual_dbr'],flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('mode',choices=['capture','analyze']);globals()[p.parse_args().mode]()
