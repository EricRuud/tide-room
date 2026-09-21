"""Locate DC rejection relative to level-dependent behavior with quiet pilots.

A silent output after a DC step does not reveal detector activity. Coherent
quiet pilots make the remaining incremental response observable. These renders
never reach the hardware audio device.
"""
import json
import numpy as np
from p821_identify import ROOT,SR,save,submit
from p821_identification_analysis import read


def main():
    carriers=[np.zeros((384*256,2))];pilots=[np.zeros_like(carriers[0])];cases=[];offset=len(carriers[0]);frequencies=[3000,9000,15000,21000]
    for layout in ['mono','left']:
        for level in [.1,.3,.7,1.2,-.7]:
            n=1125*256;a=SR;b=4*SR;t=np.arange(n)/SR;c=np.zeros((n,2));p=np.zeros_like(c);pan=np.array([1,1 if layout=='mono' else 0])
            c[a:b]=level*pan;p[:]=sum(np.sin(2*np.pi*f*t) for f in frequencies)[:,None]*.0005*pan
            carriers.append(c);pilots.append(p);cases.append(dict(start=offset,end=offset+n,conditioner_start=offset+a,conditioner_end=offset+b,level=level,layout=layout));offset+=n
    (ROOT/'dc-detector-probe-cases.json').write_text(json.dumps(cases,indent=2));c=np.concatenate(carriers);p=np.concatenate(pilots)
    for sign,label in [(1,'plus'),(-1,'minus')]:submit('dc-detector-probe-'+label,save('dc-detector-probe-'+label,c+sign*p))
    y=(read('dc-detector-probe-plus')-read('dc-detector-probe-minus'))*.5;blocks=y[:,0].reshape(-1,256);osc=np.exp(-2j*np.pi*np.arange(256)[:,None]*np.array(frequencies)[None,:]/SR);amplitudes=blocks@osc;rows=[]
    for c in cases:
        a=c['conditioner_start']//256;b=c['conditioner_end']//256;normal=np.mean(amplitudes[a-40:a-10],axis=0);relative=amplitudes[a:b+160]/normal;row=c|dict(gain_db=(20*np.log10(np.maximum(abs(relative),1e-20))).tolist(),phase_degrees=np.degrees(np.angle(relative)).tolist(),steady_gain_db=(20*np.log10(abs(np.mean(amplitudes[b-80:b-20],axis=0)/normal))).tolist());rows.append(row);print(c['layout'],c['level'],'steady pilot dB',np.round(row['steady_gain_db'],4),flush=True)
    (ROOT/'dc-detector-probe-analysis.json').write_text(json.dumps(dict(frequencies=frequencies,block_seconds=256/SR,rows=rows),indent=2))


if __name__=='__main__':main()
