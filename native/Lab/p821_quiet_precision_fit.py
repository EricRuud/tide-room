"""Describe quiet full-band EQ using higher-SNR incremental responses.

Fit only the .06-peak operating response. Other levels and weak tone-grid cases
are diagnostics. No musical waveform supplies fitting targets or parameters.
"""
import json
import numpy as np
from scipy import optimize,signal
from p821_identification_analysis import ROOT,SR,read,linear_kernel,db
from p821_quiet_eq_fit import response
from p821_slow_detector import gain_signal
from p821_steady_eq_fit import shelf
from p821_operating_fit import peak


def main():
    data=np.load(ROOT/'quiet-precision-responses.npz');r=(data['small']+data['large'])*.5;cases=json.loads((ROOT/'quiet-precision-cases.json').read_text())
    x=read('input-quiet-precision-small-plus');g=gain_signal(x);frequencies=np.fft.rfftfreq(r.shape[1],1/SR);ratios=np.fft.rfft(r,axis=1)/np.fft.rfft(r[0]);hz=np.geomspace(25,22500,600);hz=hz[abs(hz-6000)>1100]
    scalar=[];drive=[]
    for c in cases:
        s=float(np.mean(g[c['probe_start']:c['probe_start']+1024]));scalar.append(s);drive.append(max(.004703904824595531-20*np.log10(s),1e-10))
    at=lambda h:np.interp(hz,frequencies,h.real)+1j*np.interp(hz,frequencies,h.imag)
    target=at(ratios[2])/scalar[2];reports=[]
    for kind in ['peak','shelf']:
        def H(p,hz,amount):
            result=np.full(len(hz),10**(p[0]*amount/20),dtype=complex)
            for j,(f,q,gain) in enumerate(p[1:].reshape(-1,3)):
                b,a=(shelf if j==0 and kind=='shelf' else peak)(np.exp(f),np.exp(q),gain*amount)
                result*=signal.freqz(b,a,worN=hz,fs=SR)[1]
            return result
        initial=np.array([.05]+[v for f,gain in [(60,-.3),(190,-.15),(2620,-.15)] for v in [np.log(f),np.log(1.),gain]])
        lo=np.array([-.5]+[v for f in [60,190,2620] for v in [np.log(f/2),np.log(.2),-2.]])
        hi=np.array([.5]+[v for f in [60,190,2620] for v in [np.log(f*2),np.log(8.),2.]])
        def error(p):
            e=H(p,hz,drive[2])-target;return np.concatenate([e.real,e.imag])
        fit=optimize.least_squares(error,initial,bounds=(lo,hi),max_nfev=3000,ftol=1e-12,xtol=1e-12,gtol=1e-12);p=fit.x
        row=dict(kind=kind,parameters=p.tolist(),gain_db_per_db=float(p[0]),filters=[dict(frequency=float(np.exp(f)),q=float(np.exp(q)),gain_db_per_db=float(gain)) for f,q,gain in p[1:].reshape(-1,3)],operating=[])
        for i in [1,2,3]:
            tar=at(ratios[i]);pred=scalar[i]*H(p,hz,drive[i]);small=np.fft.rfft(data['small'][i])/np.fft.rfft(data['small'][0]);large=np.fft.rfft(data['large'][i])/np.fft.rfft(data['large'][0])
            row['operating'].append(dict(level=cases[i]['level'],fit=i==2,baseline_error_dbr=db(np.linalg.norm(tar-scalar[i])/np.linalg.norm(tar)),model_error_dbr=db(np.linalg.norm(tar-pred)/np.linalg.norm(tar)),probe_size_disagreement_dbr=db(np.linalg.norm(at(small-large))/np.linalg.norm(tar))))
        xx=read('input-training-grid');yy=read('training-grid');gg=gain_signal(xx);h=linear_kernel();validation=[]
        for c in json.loads((ROOT/'training-grid-cases.json').read_text()):
            if c['mode']!='mono' or c['peak']!=.05:continue
            a=c['start']+round(.7*SR);b=c['end']-128;t=np.arange(a,b)/SR;f=c['frequency'];A=np.column_stack([np.sin(2*np.pi*f*t),np.cos(2*np.pi*f*t),np.ones(len(t))]);X=np.linalg.lstsq(A,xx[a:b,0],rcond=None)[0];Y=np.linalg.lstsq(A,yy[a:b,0],rcond=None)[0];linear=np.sum((h[0,0]+h[0,1])*np.exp(-2j*np.pi*f*np.arange(h.shape[-1])/SR));ratio=(Y[0]+1j*Y[1])/(X[0]+1j*X[1])/linear;sg=float(gg[a:b].mean());amount=max(.004703904824595531-20*np.log10(sg),0);pred=sg*H(p,np.array([f]),amount)[0]
            validation.append(dict(frequency=f,baseline_error_dbr=db(abs(ratio-sg)/abs(ratio)),model_error_dbr=db(abs(ratio-pred)/abs(ratio))))
        row['weak_grid_validation']=validation;reports.append(row);print(json.dumps(row,indent=2),flush=True)
    (ROOT/'quiet-precision-description.json').write_text(json.dumps(dict(training='quiet-precision level .06 only',stationary_experiment=True,models=reports),indent=2))


if __name__=='__main__':main()
