"""Track Focus-dependent width continuously using a quiet side-channel pilot.

The same pilot/source is rendered at two Focus values. Their side ratio removes
common upstream tonal/gain processing. This does not measure absolute width
without an additional model of how the Focus parameter scales its amount.
"""
import argparse,json
import numpy as np
from p821_identify import ROOT,SR,save,submit
from p821_identification_analysis import read


def capture():
    parts=[np.zeros((384*256,2))];cases=[];offset=len(parts[0])
    for level in [.15,.2,.24,.3,.36,.48,.6,.72,1.]:
        n=960*256;t=np.arange(n)/SR;mid=level*np.sin(2*np.pi*6000*t);side=.005*np.sin(2*np.pi*1009*t)
        x=np.column_stack([mid+side,mid-side]);parts.extend([x,np.zeros((192*256,2))]);cases.append(dict(start=offset,end=offset+n,level=level));offset+=n+192*256
    (ROOT/'width-levels-cases.json').write_text(json.dumps(cases,indent=2));source=save('width-levels',np.concatenate(parts))
    for focus,label in [(1,'full'),(0,'zero')]:submit('width-levels-'+label,source,{'Stage Focus':focus})


def analyze():
    a=read('width-levels-full');b=read('width-levels-zero');n=len(a)//256*256;sa=(a[:n,0]-a[:n,1])*.5;sb=(b[:n,0]-b[:n,1])*.5
    aa=sa.reshape(-1,256);bb=sb.reshape(-1,256);gain=np.sum(aa*bb,axis=1)/np.maximum(np.sum(bb*bb,axis=1),1e-30);values=20*np.log10(np.maximum(gain,1e-20));rows=[]
    for c in json.loads((ROOT/'width-levels-cases.json').read_text()):
        start=c['start']//256;row=c|dict(checkpoints={str(t):float(np.median(values[start+round(t*SR/256):start+round(t*SR/256)+12])) for t in [.25,.5,1.,2.,3.,4.]});rows.append(row);print(row,flush=True)
    np.savez_compressed(ROOT/'width-levels-trajectory.npz',focus_ratio_db=values);(ROOT/'width-levels-analysis.json').write_text(json.dumps(rows,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['capture','analyze']);globals()[parser.parse_args().mode]()
