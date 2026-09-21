"""Resolve amplitude sensitivity in the map and confirm a large phase change."""
import argparse,json
import numpy as np
import soundfile as sf
from p821_lf_experiments import ROOT,SR,N,save,submit,save_json,digest
from p821_lf_coupling_map import response
from p821_identification_analysis import metrics


def capture():
    specs=[(6,.15,5,amp,False) for amp in [.0006,.0003,.00015]]
    specs += [(8,.8,7,amp,False) for amp in [.0012,.0048]]
    specs += [(6,.15,5,amp,True) for amp in [.0012,.0024]]
    cases=[]
    for carrier,peak,pilot_bin,amp,long in specs:
        end=32 if long else 14;n=(end+14)*N;t=np.arange(n)/N;a=8*N;b=end*N
        c=np.zeros(n);c[a:b]=peak*np.sin(2*np.pi*carrier*t[a:b]);c[a:a+24]*=np.linspace(0,1,24);c[b-24:b]*=np.linspace(1,0,24)
        pilot=amp*np.cos(2*np.pi*pilot_bin*t+.317);name=f'coupling-followup-{carrier:02d}-{round(100*peak):02d}-{pilot_bin:02d}-{round(amp*1e6):04d}-'+('long' if long else 'short')
        case=dict(name=name,carrier_bin=carrier,carrier_peak=peak,pilot_bin=pilot_bin,pilot_amplitude=amp,plateau_start=(end-3)*N,plateau_end=end*N,long=long)
        cases.append(case);save_json(ROOT/'coupling-followup-cases.json',cases)
        for sign,label in [(1,'plus'),(-1,'minus')]:submit(name+'-'+label,save(name+'-'+label,(c+sign*pilot)[:,None]*[1,1]))
        if carrier==8 and amp==.0012:submit(name+'-repeat',ROOT/f'input-{name}-plus.wav')
        if carrier==6 and amp==.0003:submit(name+'-repeat',ROOT/f'input-{name}-plus.wav')


def analyze():
    cases=json.loads((ROOT/'coupling-followup-cases.json').read_text());assert len(cases)==7
    base=json.loads((ROOT/'coupling-map-cases.json').read_text());cases += [c|dict(plateau_start=11*N,plateau_end=14*N,long=False) for c in base if (c['carrier_bin'],c['carrier_peak'],c['pilot_bin']) in [(6,.15,5),(8,.8,7)]]
    cases.append(dict(name='coupling-map-06-15-05-half',carrier_bin=6,carrier_peak=.15,pilot_bin=5,pilot_amplitude=.0012,plateau_start=11*N,plateau_end=14*N,long=False))
    cases.sort(key=lambda c:(c['carrier_bin'],c['long'],c['pilot_amplitude']));rows=[];previous={}
    for case in cases:
        name=case['name'];a=case['plateau_start'];b=case['plateau_end'];amp=case['pilot_amplitude']
        xp,_=sf.read(ROOT/f'input-{name}-plus.wav',always_2d=True);xm,_=sf.read(ROOT/f'input-{name}-minus.wav',always_2d=True)
        yp,_=sf.read(ROOT/f'{name}-plus.wav',always_2d=True);ym,_=sf.read(ROOT/f'{name}-minus.wav',always_2d=True)
        x=.5*(xp-xm);y=.5*(yp-ym);normalized=y[a:b]/amp;row=case|dict(response=response(x,y,case,a,b))
        key=(case['carrier_bin'],case['long'])
        if key in previous:row['against_previous_smaller_amplitude']=metrics(normalized,previous[key])
        previous[key]=normalized
        if (ROOT/f'{name}-repeat.wav').exists():
            rep,_=sf.read(ROOT/f'{name}-repeat.wav',always_2d=True)
            row['repeat_noise_relative_to_marginal']=metrics(y[a:b],y[a:b]+.5*(rep[a:b]-yp[a:b]))
            row['repeat_line_error']=response(x,.5*(rep-yp),case,a,b)
        rows.append(row);print(name,row['response']['direct']['gain_db'],row['response']['mirror_conjugate']['gain_db'],row['response']['mirror_conjugate']['phase_deg'],'convergence',row.get('against_previous_smaller_amplitude',{}).get('residual_dbr'),flush=True)
    save_json(ROOT/'coupling-followup-analysis.json',dict(rows=rows,post_final_exploration=True,no_fitting=True,script_sha256=digest(__file__)))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('mode',choices=['capture','analyze']);globals()[p.parse_args().mode]()
