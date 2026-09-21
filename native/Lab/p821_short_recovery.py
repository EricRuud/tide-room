"""Test a finite averaging window using shorter high-level conditioners.

Written before these responses are captured. A 256 ms moving average driven by
a 64/128 ms burst predicts a post-burst plateau before its falling edge. The
later probe measures combined gain and filter feedthrough, not an isolated stage.
"""
import argparse,json
import numpy as np
from p821_identify import ROOT,SR,save,submit
from p821_identification_analysis import read,linear_kernel,db


def capture():
    cases=[];conditioners=[np.zeros((384*256,2))];probes=[np.zeros((384*256,2))];offset=len(conditioners[0])
    for blocks in [12,24]:
        for gap in [32,64,96,128,160,192,224,256,288]:
            c=np.zeros((192*256,2));p=np.zeros_like(c);a=48*256;b=a+blocks*256
            c[a:b,0]=1.4*np.sin(2*np.pi*187.5*np.arange(b-a)/SR);pa=b+round(gap*SR/1000);p[pa,0]=.006
            cases.append(dict(start=offset,conditioner_start=offset+a,conditioner_end=offset+b,probe_start=offset+pa,level=1.4,gap_ms=gap,duration_ms=blocks*256/SR*1000,probe_amplitude=.006))
            conditioners.append(c);probes.append(p);offset+=len(c)
    c=np.concatenate(conditioners);p=np.concatenate(probes);(ROOT/'short-recovery-cases.json').write_text(json.dumps(cases,indent=2))
    for sign,suffix in [(1,'plus'),(-1,'minus')]:submit('short-recovery-'+suffix,save('short-recovery-'+suffix,c+sign*p))


def analyze():
    h=linear_kernel();w=h[:,:,0]/(h[0,0,0]+h[0,1,0]);y=(read('short-recovery-plus')-read('short-recovery-minus'))*.5@np.linalg.inv(w).T;quiet=h[0,0,0]+h[0,1,0];rows=[]
    for c in json.loads((ROOT/'short-recovery-cases.json').read_text()):
        gain=y[c['probe_start'],0]/c['probe_amplitude']/quiet;r=c|dict(direct_gain_db=db(abs(gain)));rows.append(r)
        print(c['duration_ms'],c['gap_ms'],round(r['direct_gain_db'],6),flush=True)
    (ROOT/'short-recovery-analysis.json').write_text(json.dumps(rows,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['capture','analyze']);globals()[parser.parse_args().mode]()
