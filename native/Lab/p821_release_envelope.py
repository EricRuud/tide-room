"""Dense post-burst gain curves from three coherent, quiet HF pilots.

Matched perturbations remove the conditioner. The inferred gain includes any
remaining dynamic EQ at each pilot frequency; agreement across pilots is checked
before interpreting a late tail as broadband gain recovery.
"""
import json
import numpy as np
from p821_identify import ROOT,SR,save,submit
from p821_identification_analysis import read,linear_kernel


def main():
    carriers=[np.zeros((384*256,2))];pilots=[np.zeros((384*256,2))];cases=[];offset=len(carriers[0]);frequencies=[9000,15000,21000]
    for frequency in [187.5,1500,6000]:
        for level in [.7,.9,1.1,1.4,1.8]:
            for duration in [12,24,96,192]:
                n=512*256;t=np.arange(n)/SR;a=64*256;b=a+duration*256;c=np.zeros((n,2));p=np.zeros_like(c)
                c[a:b,0]=level*np.sin(2*np.pi*frequency*np.arange(b-a)/SR)
                p[:,0]=sum(np.sin(2*np.pi*f*t) for f in frequencies)*.0005
                carriers.append(c);pilots.append(p);cases.append(dict(start=offset,end=offset+n,conditioner_start=offset+a,conditioner_end=offset+b,frequency=frequency,level=level,duration_seconds=duration*256/SR));offset+=n
    (ROOT/'release-envelope-cases.json').write_text(json.dumps(cases,indent=2));c=np.concatenate(carriers);p=np.concatenate(pilots)
    for sign,label in [(1,'plus'),(-1,'minus')]:submit('release-envelope-'+label,save('release-envelope-'+label,c+sign*p))
    differential=(read('release-envelope-plus')-read('release-envelope-minus'))*.5
    h=linear_kernel();w=h[:,:,0]/(h[0,0,0]+h[0,1,0]);y=(differential@np.linalg.inv(w).T)[:,0];blocks=y.reshape(-1,256)
    osc=np.exp(-2j*np.pi*np.arange(256)[:,None]*np.array(frequencies)[None,:]/SR);amplitudes=blocks@osc;rows=[]
    for case in cases:
        a=case['conditioner_start']//256;b=case['conditioner_end']//256;normal=np.mean(amplitudes[a-16:a-4],axis=0);relative=amplitudes[b:b+240]/normal
        gain_db=20*np.log10(np.maximum(abs(relative),1e-30));phase=np.degrees(np.angle(relative));row=case|dict(gain_db=gain_db.tolist(),phase_degrees=phase.tolist());rows.append(row)
    (ROOT/'release-envelope-analysis.json').write_text(json.dumps(dict(pilot_frequencies=frequencies,block_seconds=256/SR,rows=rows),indent=2))
    print('Captured',len(cases),'dense release curves',flush=True)


if __name__=='__main__':main()
