"""Training-only Focus pairs spanning frequency, level, and channel balance.

Randomized ordering separates level from preceding history. The quiet side pilot
keeps width observable during near-mono material and through releases. No final
musical holdout is used in this capture or in label extraction.
"""
import json
import numpy as np
from p821_identify import ROOT,SR,save,submit
from p821_identification_analysis import read
from p821_width_identify import trajectory,BASE_DIFFERENCE_DB


def main():
    rng=np.random.default_rng(94821);parts=[np.zeros((384*256,2))];cases=[];offset=len(parts[0])
    settings=[(f,a,pan) for f in [75,187.5,500,1250,2500,4500,6000,11000] for a in np.geomspace(.035,1.65,15) for pan in [1.,.55,-1.]]
    rng.shuffle(settings)
    for index,(frequency,level,pan) in enumerate(settings):
        n=224*256;t=np.arange(n)/SR;end=160*256
        pilot=.003*(np.sin(2*np.pi*1009*t)+.5*np.sin(2*np.pi*8011*t))
        carrier=np.zeros(n);carrier[:end]=level*np.sin(2*np.pi*frequency*t[:end]+rng.uniform(-np.pi,np.pi))
        x=carrier[:,None]*[1,pan]+pilot[:,None]*[1,-1];parts.append(x)
        cases.append(dict(start=offset,end=offset+n,release=offset+end,frequency=frequency,level=float(level),pan=pan));offset+=n
    (ROOT/'width-training-cases.json').write_text(json.dumps(cases,indent=2))
    source=save('width-training',np.concatenate(parts))
    for focus,label in [(1,'full'),(0,'zero')]:submit('width-training-'+label,source,{'Stage Focus':focus})
    full=read('width-training-full');zero=read('width-training-zero');values,weights,row,_=trajectory(full,zero)
    np.savez_compressed(ROOT/'width-labels-width-training.npz',focus_delta_db=values-BASE_DIFFERENCE_DB,side_energy=weights)
    (ROOT/'width-training-factorization.json').write_text(json.dumps(row,indent=2));print(row,flush=True)


if __name__=='__main__':main()
