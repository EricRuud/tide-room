"""Input-only dynamic application of the stationary quiet-EQ hypothesis.

The .06-peak operating response supplied fixed filter coefficients. Dynamic
drive comes only from the previously identified input-level detector. This
script compares music without refitting gain, EQ or time alignment to it.
"""
import json
import numpy as np
import torch
from scipy import signal
from p821_identification_analysis import ROOT,SR,read,metrics
from p821_dynamic_eq import Biquad
from p821_slow_detector import predict_curve
import p821_memory_control_model as extension


@torch.no_grad()
def correct(d):
    p=np.array(json.loads((ROOT/'quiet-eq-description.json').read_text())['parameters']);slow=np.array(list(json.loads((ROOT/'slow-detector-fit.json').read_text())[1]['parameters'].values()))
    raw=d['x'].astype(np.float64);rms=np.sqrt(np.mean(raw.reshape(-1,256,2)**2,axis=1));levels=np.mean(np.maximum(20*np.log10(np.maximum(rms,1e-10)),-90),axis=1)
    gain=predict_curve(levels,slow,'after');drive=torch.from_numpy(np.maximum(slow[4]-np.concatenate([[slow[4]],gain]),0))[None,:].repeat(2,1)
    x=d['base'].T.double()
    for f,q,g in p[1:].reshape(-1,3):
        f=np.exp(f);q=np.exp(q);A=torch.pow(10.,drive*g/40);omega=2*np.pi*f/SR;alpha=np.sin(omega)/(2*q);cos=np.cos(omega);a0=1+alpha/A
        x=Biquad.apply(x,torch.stack([(1+alpha*A)/a0,-2*cos/a0,(1-alpha*A)/a0,-2*cos/a0,(1-alpha/A)/a0],dim=2))
    fraction=torch.arange(1,257,dtype=torch.float64)[None,None,:]/256;db=(drive[:,:-1,None]*(1-fraction)+drive[:,1:,None]*fraction).flatten(1,2)*p[0]
    output=dict(d);output['base']=(x*torch.pow(10.,db/20)).T.contiguous();return output


def main():
    extension.activate();core=extension.core;torch.set_num_threads(2);checkpoint=torch.load(ROOT/'surrogate-v18-selected.pt',weights_only=True);m=core.Model();m.load_state_dict(checkpoint['state_dict']);m.eval();report={}
    for name in ['music-levels','detector-pulses','quality-tones']:
        d=core.prepare_source(json.loads((ROOT/(name+'.json')).read_text())['job']['input']);target=read(name);base=core.predict(m,d);changed=core.predict(m,correct(d));row=dict(original=metrics(target[SR:],base[SR:]),corrected=metrics(target[SR:],changed[SR:]))
        if name=='music-levels':
            row['passages']=[]
            for c in json.loads((ROOT/'input-music-levels.json').read_text()):
                a,b=c['start'],c['end'];r=c|dict(original=metrics(target[a:b],base[a:b]),corrected=metrics(target[a:b],changed[a:b]));row['passages'].append(r);print(c['gain_db'],r['original']['residual_dbr'],r['corrected']['residual_dbr'],flush=True)
        report[name]=row;print(name,row['original']['residual_dbr'],row['corrected']['residual_dbr'],flush=True)
        (ROOT/'quiet-correction-v18.json').write_text(json.dumps(dict(checkpoint_step=checkpoint['step'],fitted_on_music=False,results=report),indent=2))


if __name__=='__main__':main()
