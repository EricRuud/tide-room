"""Describe the measured moving clipping ceiling from flat waveform regions.

Uses target audio to identify an envelope hypothesis. It is not a complete model,
and flat-region selection must not be confused with full-waveform validation.
"""
import json
import numpy as np
from scipy import optimize
from p821_identification_analysis import ROOT,SR,read,linear_kernel,db
from p821_slow_detector import predict_curve

BLOCK=256


def main():
    x=read('input-saturation-map');y=read('saturation-map');h=linear_kernel();w=h[:,:,0]/(h[0,0,0]+h[0,1,0]);y=y@np.linalg.inv(w).T
    levels=20*np.log10(np.maximum(np.sqrt(np.mean(x.reshape(-1,BLOCK,2)**2,axis=1)),1e-12))
    candidates=[]
    for c in json.loads((ROOT/'saturation-map-cases.json').read_text()):
        if c['frequency']>350:continue
        a=c['start']+round(.03*SR);b=c['start']+round(.23*SR)
        v=y[a:b,0];curvature=np.abs(v[2:]-2*v[1:-1]+v[:-2])
        chosen=np.flatnonzero((curvature<2e-6)&(abs(v[1:-1])>.78)) + a+1
        candidates.extend(chosen.tolist())
    pos=np.array(candidates);rng=np.random.default_rng(28821)
    pos=np.sort(rng.choice(pos,min(6000,len(pos)),replace=False))
    target=20*np.log10(abs(y[pos,0]));frame=pos//BLOCK;fraction=(pos%BLOCK+1)/BLOCK
    rows=[]
    for placement in ['before','after']:
        def prediction(p):
            floor,ratio,tau,ceiling=p
            shared=np.mean(np.maximum(levels,floor),axis=1)
            gain=-predict_curve(shared,[47,tau,-45.1065,1,0],placement)
            previous=np.concatenate([[gain[0]],gain[:-1]])
            return ceiling+ratio*(previous[frame]*(1-fraction)+gain[frame]*fraction)
        def error(p):return prediction(p)-target
        fit=optimize.least_squares(error,[-90,.5,.02,db(.795)],bounds=([-160,.01,.0001,-3],[-50,3,.1,-1]),loss='soft_l1',f_scale=.02,max_nfev=1000,ftol=1e-12,xtol=1e-12,gtol=1e-12)
        rows.append(dict(placement=placement,parameters=dict(zip(['silence_floor_db','ceiling_growth_per_db','tau_seconds','ceiling_db'],fit.x.tolist())),selected_points=len(pos),selected_ceiling_rmse_db=float(np.sqrt(np.mean(error(fit.x)**2)))))
        print(rows[-1],flush=True)
    (ROOT/'adaptive-clip-envelope-fit.json').write_text(json.dumps(rows,indent=2))


if __name__=='__main__':main()
