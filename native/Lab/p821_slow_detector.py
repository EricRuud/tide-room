"""Identify the slow, mild level component separately from driven EQ changes.

Hypotheses: mean channel dB levels, finite averaging window, and a thresholded
gain curve. Tonal identification cannot distinguish RMS from peak calibration.
The pulse recording is withheld from parameter fitting.
"""
import json
import numpy as np
from scipy import signal,optimize
from p821_identification_analysis import ROOT,SR,read,linear_kernel,linear_render,metrics,db

BLOCK=256


def descriptors(x,kind='rms'):
    x=np.pad(x,((0,(-len(x))%BLOCK),(0,0))).reshape(-1,BLOCK,2)
    if kind=='rms':amplitude=np.sqrt(np.mean(x*x,axis=1))
    elif kind=='peak':amplitude=np.max(abs(x),axis=1)
    elif kind=='absolute':amplitude=np.mean(abs(x),axis=1)
    else:raise ValueError(kind)
    return np.mean(20*np.log10(np.maximum(amplitude,1e-10)),axis=1)


def smooth(values,window,tau):
    # A fractional box length interpolates adjacent integer-length hypotheses.
    n=int(np.floor(window));fraction=window-n
    def box(length):
        padded=np.pad(values,(length,0),constant_values=values[0]);c=np.cumsum(padded)
        return (c[length:]-c[:-length])/length
    result=box(n)*(1-fraction)+box(n+1)*fraction
    if tau>0:
        alpha=np.exp(-BLOCK/(SR*tau))
        result=signal.lfilter([1-alpha],[1,-alpha],result,zi=[alpha*result[0]])[0]
    return result


def predict_curve(levels,p,placement='after'):
    window,tau,threshold,slope,offset=p
    if placement=='before':
        level=smooth(levels,window,tau)
        gain=-slope*np.maximum(level-threshold,0)
    else:
        level=smooth(levels,window,0)
        gain=-slope*np.maximum(level-threshold,0)
        alpha=np.exp(-BLOCK/(SR*tau))
        gain=signal.lfilter([1-alpha],[1,-alpha],gain,zi=[alpha*gain[0]])[0]
    return offset+gain


def demod(name):
    x=read('input-'+name);y=read(name)
    omega=2*np.pi*1500/SR;h=linear_kernel()
    H=np.sum((h[0,0]+h[0,1])*np.exp(-1j*omega*np.arange(h.shape[-1])))
    osc=np.exp(-1j*omega*np.arange(BLOCK))
    n=len(x)//BLOCK*BLOCK
    X=x[:n,0].reshape(-1,BLOCK)@osc;Y=y[:n,0].reshape(-1,BLOCK)@osc
    gain=20*np.log10(np.maximum(abs(Y/(np.where(abs(X)>1e-10,X,1)*H)),1e-20))
    return x,gain,abs(X)>1e-10


def gain_signal(x,kind='rms',placement='after'):
    row=json.loads((ROOT/'slow-detector-fit.json').read_text())[1]
    p=np.array(list(row['parameters'].values()))
    tone=np.sin(2*np.pi*1500*np.arange(BLOCK)/SR)
    amplitudes={'rms':np.sqrt(np.mean(tone*tone)),'peak':np.max(abs(tone)),'absolute':np.mean(abs(tone))}
    p[2]+=20*np.log10(amplitudes[kind]/amplitudes['rms'])
    g=predict_curve(descriptors(x,kind),p,placement)
    fraction=np.arange(1,BLOCK+1)[None,:]/BLOCK
    previous=np.concatenate([[p[4]],g[:-1]])
    samples=(previous[:,None]*(1-fraction)+g[:,None]*fraction).reshape(-1)[:len(x)]
    return 10**(samples/20)


def audit():
    rows={};h=linear_kernel()
    for name in ['training-noise','structure-mono','structure-side','structure-left','structure-pan','music-levels','detector-pulses']:
        job=json.loads((ROOT/(name+'.json')).read_text())['job']
        from p821_bandlimited_dictionary import raw_input
        x=raw_input(job['input']);y=read(name);base=linear_render(x,h)
        rows[name]={}
        for kind in ['rms','peak','absolute']:
            gain=gain_signal(x,kind)
            for order in ['pre','post']:
                pred=linear_render(x*gain[:,None],h) if order=='pre' else base*gain[:,None]
                key=f'{kind}-{order}';row=metrics(y[SR:],pred[SR:])
                if name=='music-levels':
                    row['passages']=[c|metrics(y[c['start']:c['end']],pred[c['start']:c['end']]) for c in json.loads((ROOT/'input-music-levels.json').read_text())]
                if name.startswith('structure-'):
                    row['quiet_segments']=metrics(y[3*SR:11*SR],pred[3*SR:11*SR])
                rows[name][key]=row
                print(name,key,round(row['residual_dbr'],2),('quiet '+str(round(row['quiet_segments']['residual_dbr'],2))) if 'quiet_segments' in row else '',flush=True)
        (ROOT/'slow-detector-audit.json').write_text(json.dumps(rows,indent=2))


def main():
    x,target,mask=demod('long-levels');levels=descriptors(x)
    # Only levels <= 0.15 before the first stronger segment are used for fitting.
    cases=json.loads((ROOT/'long-level-cases.json').read_text())
    use=np.zeros(len(levels),dtype=bool)
    for c in cases[:4]:use[c['start']//BLOCK+5:c['end']//BLOCK]=True
    xp,tp,mp=demod('detector-pulses');lp=descriptors(xp)
    # Exclude the short carrier/filter transient at the start of the record.
    mp[:SR//BLOCK]=False
    rows=[]
    for placement in ['before','after']:
        def error(p):return predict_curve(levels,p,placement)[use]-target[use]
        result=optimize.least_squares(error,[48,.02,-45,.029,.005],bounds=([10,.00001,-65,.005,-.1],[100,.2,-25,.1,.1]),max_nfev=1000,ftol=1e-13,xtol=1e-13,gtol=1e-13)
        p=result.x;yp=predict_curve(lp,p,placement)
        row=dict(smoothing_placement=placement,parameters=dict(zip(['window_blocks','tau_seconds','threshold_db_rms','slope','offset_db'],p.tolist())),training_gain_rmse_db=float(np.sqrt(np.mean(error(p)**2))),pulse_validation_gain_rmse_db=float(np.sqrt(np.mean((yp[mp]-tp[mp])**2))))
        rows.append(row);print(row,flush=True)
    (ROOT/'slow-detector-fit.json').write_text(json.dumps(rows,indent=2))


if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['fit','audit'],default='fit',nargs='?')
    main() if parser.parse_args().mode=='fit' else audit()
