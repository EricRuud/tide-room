"""Isolate Focus-dependent side gain from matched broadband renders.

Primary observations are ratios between two reference renders, not emulation
scores. Estimate one gain per 256-sample block and retain fitting residuals.
"""
import json
import numpy as np
from p821_identification_analysis import ROOT,SR,read,metrics


def main():
    report={}
    for mode in ['left','side','pan','anti-pan']:
        a=read('structure-'+mode);b=read('structure-focus-zero-'+mode);n=len(a)//256*256
        mid_a=a[:n].mean(axis=1);mid_b=b[:n].mean(axis=1);sa=(a[:n,0]-a[:n,1])*.5;sb=(b[:n,0]-b[:n,1])*.5
        aa=sa.reshape(-1,256);bb=sb.reshape(-1,256);gain=np.sum(aa*bb,axis=1)/np.maximum(np.sum(bb*bb,axis=1),1e-30);pred=(bb*gain[:,None]).reshape(-1)
        row=dict(mid=metrics(mid_a,mid_b),side_block_gain_fit=metrics(sa,pred),segments=[])
        for start,level in [(3,.05),(7,.2),(11,.7)]:
            frames=np.arange(round((start+3)*SR)//256,round((start+3.8)*SR)//256)
            values=20*np.log10(np.maximum(gain[frames],1e-20));row['segments'].append(dict(level=level,focus_side_gain_difference_db=float(np.median(values)),minimum_db=float(np.min(values)),maximum_db=float(np.max(values))))
        report[mode]=row;print(mode,row,flush=True)
        np.savez_compressed(ROOT/f'focus-trajectory-{mode}.npz',gain_db=20*np.log10(np.maximum(gain,1e-20)),input=read('input-structure-'+mode)[::256])
    (ROOT/'focus-structure-analysis.json').write_text(json.dumps(report,indent=2))


if __name__=='__main__':main()
