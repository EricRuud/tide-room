"""Test a small tonal change proportional to the identified gentle gain reduction.

Fit only the .06-peak 6 kHz operating response. Other quiet operating levels and
weak training-grid tones test that proportionality. This is a stationary response
experiment; dynamic musical prediction is a separate required check.
"""
import json
import numpy as np
from scipy import signal,optimize
from p821_identification_analysis import ROOT,SR,read,linear_kernel,db
from p821_operating_fit import peak
from p821_slow_detector import gain_signal


def response(p,hz,drive):
    H=np.full(np.shape(hz),10**(p[0]*drive/20),dtype=complex)
    for f,q,g in p[1:].reshape(-1,3):
        b,a=peak(np.exp(f),np.exp(q),g*drive);H*=signal.freqz(b,a,worN=hz,fs=SR)[1]
    return H


def main():
    data=np.load(ROOT/'quiet-operating-responses.npz');r=(data['small']+data['large'])*.5;cases=json.loads((ROOT/'quiet-operating-cases.json').read_text())
    x=read('input-quiet-operating-small-plus');g=gain_signal(x);frequency=np.fft.rfftfreq(r.shape[1],1/SR);Q=np.fft.rfft(r[0]);ratios=np.fft.rfft(r,axis=1)/Q;drives=[];scalars=[]
    for c in cases:
        scalar=float(np.mean(g[c['probe_start']:c['probe_start']+1024]));scalars.append(scalar);drives.append(max(.004703904824595531-20*np.log10(scalar),1e-10))
    hz=np.geomspace(300,22500,350);hz=hz[abs(hz-6000)>1100];train_index=2
    target=np.interp(hz,frequency,ratios[train_index].real)+1j*np.interp(hz,frequency,ratios[train_index].imag);target/=scalars[train_index]
    p=np.array([0.]+[v for f in [180,2200,9000] for v in [np.log(f),np.log(.8),-.1]])
    lower=np.array([-.3]+[v for f in [180,2200,9000] for v in [np.log(f/2),np.log(.2),-1.]])
    upper=np.array([.3]+[v for f in [180,2200,9000] for v in [np.log(min(f*2,23000)),np.log(6),1.]])
    def error(p):
        e=response(p,hz,drives[train_index])-target
        return np.concatenate([e.real,e.imag])
    fit=optimize.least_squares(error,p,bounds=(lower,upper),max_nfev=1500,ftol=1e-12,xtol=1e-12,gtol=1e-12);p=fit.x
    report=dict(training='quiet-operating level .06 only',stationary_experiment=True,parameters=p.tolist(),filters=[dict(frequency=float(np.exp(f)),q=float(np.exp(q)),gain_db_per_db_reduction=float(gain)) for f,q,gain in p[1:].reshape(-1,3)],gain_db_per_db_reduction=float(p[0]),operating=[])
    for i in [1,2,3]:
        tar=np.interp(hz,frequency,ratios[i].real)+1j*np.interp(hz,frequency,ratios[i].imag);base=np.full(len(hz),scalars[i]);pred=scalars[i]*response(p,hz,drives[i])
        row=dict(level=cases[i]['level'],fit=i==train_index,baseline_relative_complex_error_dbr=db(np.linalg.norm(tar-base)/np.linalg.norm(tar)),model_relative_complex_error_dbr=db(np.linalg.norm(tar-pred)/np.linalg.norm(tar)));report['operating'].append(row);print(row,flush=True)
    x=read('input-training-grid');y=read('training-grid');g=gain_signal(x);h=linear_kernel();validation=[]
    for c in json.loads((ROOT/'training-grid-cases.json').read_text()):
        if c['mode']!='mono' or c['peak']!=.05:continue
        a=c['start']+round(.7*SR);b=c['end']-128;t=np.arange(a,b)/SR;f=c['frequency'];A=np.column_stack([np.sin(2*np.pi*f*t),np.cos(2*np.pi*f*t),np.ones(len(t))]);X=np.linalg.lstsq(A,x[a:b,0],rcond=None)[0];Y=np.linalg.lstsq(A,y[a:b,0],rcond=None)[0];H=np.sum((h[0,0]+h[0,1])*np.exp(-2j*np.pi*f*np.arange(h.shape[-1])/SR));ratio=(Y[0]+1j*Y[1])/(X[0]+1j*X[1])/H;scalar=float(g[a:b].mean());drive=max(.004703904824595531-20*np.log10(scalar),0);prediction=scalar*response(p,np.array([f]),drive)[0]
        row=dict(frequency=f,baseline_error_dbr=db(abs(ratio-scalar)/abs(ratio)),model_error_dbr=db(abs(ratio-prediction)/abs(ratio)));validation.append(row);print(row,flush=True)
    report['weak_grid_validation']=validation;(ROOT/'quiet-eq-description.json').write_text(json.dumps(report,indent=2))


if __name__=='__main__':main()
