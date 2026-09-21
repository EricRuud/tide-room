"""Test bipolar small-signal kernels against the original positive-impulse IR.

All alternative kernels come from synthetic impulses, never fitted music. Only
the quiet music passage is used here; this is ongoing model-development
validation, not one of the preserved final musical holdouts.
"""
import json
import numpy as np
from p821_identification_analysis import ROOT,SR,read,linear_kernel,linear_render,metrics
from p821_slow_detector import predict_curve


def main():
    source=read('input-music-levels');target=read('music-levels');c=json.loads((ROOT/'input-music-levels.json').read_text())[0];n=(c['end']+255)//256*256;x=source[:n];rms=np.sqrt(np.mean(x.reshape(-1,256,2)**2,axis=1));levels=np.mean(np.maximum(20*np.log10(np.maximum(rms,1e-10)),-90),axis=1)
    p=np.array(list(json.loads((ROOT/'slow-detector-fit.json').read_text())[1]['parameters'].values()));g=predict_curve(levels,p,'after');old=np.concatenate([[p[4]],g[:-1]]);fraction=np.arange(1,257)[None,:]/256;gain=10**((old[:,None]*(1-fraction)+g[:,None]*fraction).reshape(-1)/20)
    h=linear_kernel();w=h[:,:,0]/(h[0,0,0]+h[0,1,0]);responses=np.load(ROOT/'weak-impulse-map-responses.npz')['responses'];cases=json.loads((ROOT/'weak-impulse-map-cases.json').read_text());kernels={'original_positive':h}
    for amplitude in [.00025,.001,.004,.016,.064]:
        rr=np.mean([r for r,case in zip(responses,cases) if case['amplitude']==amplitude],axis=0)@np.linalg.inv(w).T
        kernels[f'bipolar_{amplitude}']=w[:,:,None]*rr[:,0][None,None,:]
    rows={}
    for name,kernel in kernels.items():
        y=linear_render(x,kernel)*gain[:,None];row=metrics(target[c['start']:c['end']],y[c['start']:c['end']]);rows[name]=row;print(name,row,flush=True)
    (ROOT/'quiet-baseline-audit.json').write_text(json.dumps(dict(fitted_on_music=False,passage=c,results=rows),indent=2))


if __name__=='__main__':main()
