"""Candidate rectified and peak detectors with separate attack and release.

These are a hypothesis bank for identification, not measured P821 time constants.
Only boundary states are retained. Whole-buffer descriptors use the same 256
sample frame convention as the captures.
"""
import numpy as np
from scipy import signal
from numba import njit

SR=48000
BLOCK=256
PAIRS=[(attack,release) for attack in [.0005,.005,.02] for release in [.02,.08,.3]]
FRAME_PAIRS=[(0.,.02),(.005,.08),(.02,.3)]


@njit(cache=True)
def follow(x,attack,release,stride):
    result=np.empty((len(x)//stride,x.shape[1],len(attack)))
    states=np.zeros((x.shape[1],len(attack)))
    for n in range(len(x)):
        for c in range(x.shape[1]):
            value=abs(x[n,c])
            for j in range(len(attack)):
                coefficient=attack[j] if value>states[c,j] else release[j]
                states[c,j]=value+coefficient*(states[c,j]-value)
        if (n+1)%stride==0:result[n//stride]=states
    return result


def band_features(x):
    attack=np.exp(-1/(SR*np.array([a for a,r in PAIRS])))
    release=np.exp(-1/(SR*np.array([r for a,r in PAIRS])))
    sample=follow(x,attack,release,BLOCK)
    a=np.array([0 if a==0 else np.exp(-BLOCK/(SR*a)) for a,r in FRAME_PAIRS])
    r=np.exp(-BLOCK/(SR*np.array([r for a,r in FRAME_PAIRS])))
    blocks=x.reshape(-1,BLOCK,2)
    descriptors=[np.mean(abs(blocks),axis=1),np.max(abs(blocks),axis=1),np.sqrt(np.mean(blocks*blocks,axis=1))]
    result=np.concatenate([sample]+[follow(d,a,r,1) for d in descriptors],axis=2)
    return np.clip(np.log10(np.maximum(result,1e-6))/3+1,-1,1).astype(np.float32)


def features(x):
    assert len(x)%BLOCK==0
    all_bands=[band_features(x)]
    for frequency,kind in [(300,'lowpass'),(3000,'highpass')]:
        band=signal.sosfilt(signal.butter(2,frequency,btype=kind,fs=SR,output='sos'),x,axis=0)
        all_bands.append(band_features(band))
    own=np.concatenate(all_bands,axis=2)
    lr=np.concatenate([own,own[:,::-1]],axis=2)
    ms=np.column_stack([x.mean(axis=1),(x[:,0]-x[:,1])*.5])
    spatial=band_features(ms)
    spatial=spatial.reshape(len(spatial),1,-1)
    return np.concatenate([lr,np.repeat(spatial,2,axis=1)],axis=2)
