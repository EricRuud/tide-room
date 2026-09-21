"""Level-dependent incremental responses around coherent 6 kHz carriers.

Two perturbation sizes check numerical precision and local linearity. The probe
lands at a carrier zero crossing, keeping raw block RMS equal for its two signs.
Internal detectors may still move; responses near the carrier need caution.
"""
import argparse,json
import numpy as np
from p821_identify import ROOT,SR,save,submit
from p821_identification_analysis import read,linear_kernel,db


def capture():
    carriers=[np.zeros((384*256,2))];impulses=[np.zeros((384*256,2))];cases=[];offset=len(carriers[0])
    for level in [0.,.03,.06,.12,.24,.48,.72,1.,1.4]:
        n=384*256;c=np.zeros((n,2));p=np.zeros_like(c);t=np.arange(288*256)/SR
        c[:len(t)]=level*np.sin(2*np.pi*6000*t)[:,None];pa=192*256;p[pa,0]=1.
        carriers.append(c);impulses.append(p);cases.append(dict(start=offset,end=offset+n,level=level,carrier_frequency=6000,probe_start=offset+pa));offset+=n
    c=np.concatenate(carriers);p=np.concatenate(impulses);(ROOT/'quiet-operating-cases.json').write_text(json.dumps(cases,indent=2))
    for amplitude,label in [(.006,'small'),(.012,'large')]:
        for sign,suffix in [(1,'plus'),(-1,'minus')]:submit(f'quiet-operating-{label}-{suffix}',save(f'quiet-operating-{label}-{suffix}',c+sign*amplitude*p))


def analyze():
    h=linear_kernel();w=h[:,:,0]/(h[0,0,0]+h[0,1,0]);rows=[];responses={}
    cases=json.loads((ROOT/'quiet-operating-cases.json').read_text())
    for amplitude,label in [(.006,'small'),(.012,'large')]:
        y=(read(f'quiet-operating-{label}-plus')-read(f'quiet-operating-{label}-minus'))/(2*amplitude)@np.linalg.inv(w).T
        responses[label]=np.array([y[c['probe_start']:c['probe_start']+12288,0] for c in cases])
    for i,c in enumerate(cases):
        r=c.copy()
        for label,rr in responses.items():
            H=np.fft.rfft(rr[i]);Q=np.fft.rfft(rr[0]);hz=np.fft.rfftfreq(rr.shape[1],1/SR)
            r[label]={str(f):dict(gain_db=db(abs(H[k]/Q[k])),phase_degrees=float(np.degrees(np.angle(H[k]/Q[k])))) for f in [50,100,180,500,1000,2700,8000,16000,22000] for k in [int(np.argmin(abs(hz-f))) ]}
        rows.append(r);print(c['level'],r['small'],flush=True)
    np.savez_compressed(ROOT/'quiet-operating-responses.npz',**responses)
    (ROOT/'quiet-operating-analysis.json').write_text(json.dumps(rows,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['capture','analyze']);globals()[parser.parse_args().mode]()
