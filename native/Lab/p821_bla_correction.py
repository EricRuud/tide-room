"""Test a weak level-dependent linear branch derived only from flat multisines.

The two kernels use the .004/.04 RMS stationary measurements. Causal truncation
and input/output placement are hypotheses tested on ongoing validation music;
they are not a recovered internal P821 architecture or final holdout scores.
"""
import json
import numpy as np
from scipy import signal
from p821_identification_analysis import ROOT,SR,read,linear_kernel,linear_render,metrics,db
from p821_slow_detector import predict_curve

N=65536


def curves(x,own=False):
    rms=np.sqrt(np.mean(x.reshape(-1,256,2)**2,axis=1));levels=np.maximum(20*np.log10(np.maximum(rms,1e-10)),-90)
    if not own:levels=np.repeat(levels.mean(axis=1)[:,None],2,axis=1)
    p=np.array(list(json.loads((ROOT/'slow-detector-fit.json').read_text())[1]['parameters'].values()));g=np.column_stack([predict_curve(levels[:,c],p,'after') for c in range(2)]);previous=np.vstack([[p[4],p[4]],g[:-1]]);fraction=np.arange(1,257)[None,:,None]/256;samples=(previous[:,None,:]*(1-fraction)+g[:,None,:]*fraction).reshape(-1,2)
    return 10**(samples/20),np.maximum(p[4]-samples,0)


def kernels():
    data=np.load(ROOT/'quiet-multisine-analysis.npz');frequency=data['frequency'];low=data['averaged'][1];high=data['averaged'][3];h=linear_kernel();w=h[:,:,0]/(h[0,0,0]+h[0,1,0]);original=np.fft.rfft(h[0,0]+h[0,1],N);hz=np.fft.rfftfreq(N,1/SR)
    x=read('input-quiet-multisine');_,drive=curves(x);cases=json.loads((ROOT/'quiet-multisine-cases.json').read_text());amount=np.mean([np.mean(drive[c['start']+N:c['end']]) for c in cases if c['shape']=='flat' and c['rms']==.04])
    def full(response):
        # Interpolate the small relative correction, retaining the measured base
        # response outside the excited band instead of inventing LF/DC gain.
        ratio=response/data['original_response']
        rr=np.interp(hz,frequency,ratio.real)+1j*np.interp(hz,frequency,ratio.imag);rr[-1]=rr[-1].real;result=original*rr;result[0]=0.;return result
    H0=full(low);H1=(full(high)-H0)/amount;k0=np.fft.irfft(H0,N);k1=np.fft.irfft(H1,N)
    report=dict(training='flat random-phase multisine, .004 and .04 RMS only',drive_db=float(amount),base_negative_lag_energy_dbr=db(np.linalg.norm(k0[N//2:])/np.linalg.norm(k0)),correction_negative_lag_energy_dbr=db(np.linalg.norm(k1[N//2:])/np.linalg.norm(k1)))
    k0=k0[:N//2].copy();k1=k1[:N//2].copy()
    # A gentle tail fade avoids a hard FIR truncation; preserve exact zero DC.
    for k in [k0,k1]:
        k[-4096:]*=np.linspace(1,0,4096);k-=k.sum()*np.hanning(len(k))/np.hanning(len(k)).sum()
    return h,w,k0,k1,report


def main():
    h,w,k0,k1,report=kernels();rows={};np.savez_compressed(ROOT/'quiet-bla-kernels.npz',base=k0,correction=k1,w=w)
    for name in ['music-levels','detector-pulses']:
        x=read('input-'+name);target=read(name)
        if name=='music-levels':case=json.loads((ROOT/'input-music-levels.json').read_text())[0];n=(case['end']+255)//256*256;x=x[:n];target=target[:n];a,b=case['start'],case['end']
        else:a,b=SR,len(x)
        gain,_=curves(x);original=linear_render(x,h);newbase=np.column_stack([signal.fftconvolve(x[:,c],k0)[:len(x)] for c in range(2)])@w.T
        variants={'original':original*gain,'new_baseline_only':newbase*gain}
        for own in [False,True]:
            _,drive=curves(x,own)
            for placement in ['pre','post']:
                source=x*drive if placement=='pre' else x
                correction=np.column_stack([signal.fftconvolve(source[:,c],k1)[:len(x)] for c in range(2)])
                if placement=='post':correction*=drive
                correction=correction@w.T
                for base,label in [(original,'old'),(newbase,'new')]:variants[f'{label}_base_{"own" if own else "shared"}_{placement}']=(base+correction)*gain
        rows[name]={key:metrics(target[a:b],y[a:b]) for key,y in variants.items()};print(name,{k:round(v['residual_dbr'],3) for k,v in rows[name].items()},flush=True)
    report['results']=rows;report['fitted_on_music']=False;(ROOT/'quiet-bla-correction-audit.json').write_text(json.dumps(report,indent=2))


if __name__=='__main__':main()
