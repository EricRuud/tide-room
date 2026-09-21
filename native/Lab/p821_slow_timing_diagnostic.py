"""Sub-block audit of gentle gain timing; synthetic development signals only.

Fits the first three low-level plateaus, below the fast-controller activity gate.
Fits use one-carrier-cycle projections; full waveform residuals remain raw.
No conclusion about P821's internal interpolation follows from a better fit.
"""
import json,time
import numpy as np
from scipy import optimize
from p821_identification_analysis import ROOT,SR,read,linear_kernel,linear_render,metrics
from p821_slow_detector import predict_curve

BLOCK=256;CYCLE=32
MODES=['linear','exponential','current-frame','half-block-extrapolation']


def curve(levels,p,mode,fraction):
    current=predict_curve(levels,p,'after');previous=np.r_[p[4],current[:-1]]
    if mode=='exponential':fraction=-np.expm1(-fraction*BLOCK/(SR*p[1]))/-np.expm1(-BLOCK/(SR*p[1]))
    elif mode=='current-frame':fraction=np.ones_like(fraction)
    elif mode=='half-block-extrapolation':fraction=fraction+.5
    return previous[:,None]+(current-previous)[:,None]*fraction


def prepare(name):
    x=read('input-'+name);target=read(name);n=len(x)//BLOCK*BLOCK;x=x[:n];target=target[:n]
    base=linear_render(x,linear_kernel())
    rms=np.sqrt(np.mean(x.reshape(-1,BLOCK,2)**2,axis=1));levels=np.mean(np.maximum(20*np.log10(np.maximum(rms,1e-10)),-90),axis=1)
    weights=base[:,0].reshape(-1,8,32)**2;denom=weights.sum(axis=2)
    numerator=(base[:,0]*target[:,0]).reshape(-1,8,32).sum(axis=2)
    gain=20*np.log10(np.maximum(numerator/np.maximum(denom,1e-30),1e-20))
    t=np.arange(1,BLOCK+1).reshape(1,8,32)/BLOCK
    fraction=(weights*t).sum(axis=2)/np.maximum(denom,1e-30)
    return dict(x=x,target=target,base=base,levels=levels,gain=gain,fraction=fraction,energy=denom)


def score(d,p,mode,mask):
    n=len(d['x']);g=curve(d['levels'],p,mode,np.arange(1,BLOCK+1)[None,:]/BLOCK)
    predicted=d['base']*10**(g.reshape(-1,1)/20)
    projection=(predicted[:,0]*d['base'][:,0]).reshape(-1,8,32).sum(axis=2)/np.maximum(d['energy'],1e-30)
    actual_db=20*np.log10(np.maximum(projection,1e-20));approx=curve(d['levels'],p,mode,d['fraction'])
    indices=np.flatnonzero(mask.reshape(-1));samples=(indices[:,None]*32+np.arange(32)).reshape(-1)
    return dict(gain_rmse_db=float(np.sqrt(np.mean((actual_db[mask]-d['gain'][mask])**2))),
                gain_projection_approximation_max_db=float(np.max(abs(actual_db[mask]-approx[mask]))),
                raw_waveform=metrics(d['target'][samples],predicted[samples]))


def main():
    started=time.monotonic();p=np.array(list(json.loads((ROOT/'slow-detector-fit.json').read_text())[1]['parameters'].values()))
    training=prepare('long-levels');pulse=prepare('detector-pulses')
    times=(np.arange(training['gain'].size)*CYCLE+CYCLE/2).reshape(training['gain'].shape)
    mask=np.zeros_like(times,dtype=bool);transient=mask.copy()
    for case in json.loads((ROOT/'long-level-cases.json').read_text())[:3]:
        mask|=(times>=case['start']+5*BLOCK)&(times<case['end'])
        transient|=(times>=case['start']+5*BLOCK)&(times<case['start']+SR)
    pulse_mask=(pulse['energy']>1e-8);pulse_mask[:SR//BLOCK]=False
    report=dict(scope='First three weak synthetic long-level plateaus; pulses are diagnostic development data',
                final_holdouts_used=False,known_parameters=p.tolist(),rows=[])
    for mode in MODES:
        row=dict(mode=mode,unfitted_training=score(training,p,mode,mask),unfitted_transients=score(training,p,mode,transient),unfitted_pulses=score(pulse,p,mode,pulse_mask))
        if mode in ['linear','exponential']:
            result=optimize.least_squares(lambda q:(curve(training['levels'],q,mode,training['fraction'])-training['gain'])[mask],p,
                bounds=([10,.0001,-65,.005,-.1],[100,.2,-25,.1,.1]),max_nfev=400,ftol=1e-11,xtol=1e-11,gtol=1e-11)
            row.update(fitted_parameters=result.x.tolist(),fit_success=bool(result.success),fit_nfev=result.nfev,
                fitted_training=score(training,result.x,mode,mask),fitted_transients=score(training,result.x,mode,transient),fitted_pulses=score(pulse,result.x,mode,pulse_mask))
        report['rows'].append(row);print(json.dumps(row),flush=True)
    report['seconds']=time.monotonic()-started;(ROOT/'slow-timing-diagnostic.json').write_text(json.dumps(report,indent=2))


if __name__=='__main__':main()
