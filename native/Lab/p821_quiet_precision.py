"""Higher-SNR quiet incremental responses with a longer impulse tail.

Two larger perturbations check local linearity. Carrier blocks have equal RMS
for both probe signs; the carrier bin is excluded from response descriptions.
"""
import json
import numpy as np
from p821_identify import ROOT,SR,save,submit
from p821_identification_analysis import read,linear_kernel


def main():
    carriers=[np.zeros((384*256,2))];probes=[np.zeros((384*256,2))];cases=[];offset=len(carriers[0])
    for level in [0.,.03,.06,.12]:
        n=576*256;t=np.arange(n)/SR;c=level*np.sin(2*np.pi*6000*t)[:,None]*np.ones((1,2));p=np.zeros_like(c);pa=192*256;p[pa,0]=1
        carriers.append(c);probes.append(p);cases.append(dict(start=offset,end=offset+n,level=level,probe_start=offset+pa));offset+=n
    carrier=np.concatenate(carriers);probe=np.concatenate(probes);(ROOT/'quiet-precision-cases.json').write_text(json.dumps(cases,indent=2));responses={}
    h=linear_kernel();w=h[:,:,0]/(h[0,0,0]+h[0,1,0])
    for amplitude,label in [(.025,'small'),(.05,'large')]:
        for sign,suffix in [(1,'plus'),(-1,'minus')]:submit(f'quiet-precision-{label}-{suffix}',save(f'quiet-precision-{label}-{suffix}',carrier+sign*amplitude*probe))
        y=(read(f'quiet-precision-{label}-plus')-read(f'quiet-precision-{label}-minus'))/(2*amplitude)@np.linalg.inv(w).T
        responses[label]=np.array([y[c['probe_start']:c['probe_start']+32768,0] for c in cases])
    np.savez_compressed(ROOT/'quiet-precision-responses.npz',**responses)
    print('Quiet precision capture complete; larger probe linearity must be checked',flush=True)


if __name__=='__main__':main()
