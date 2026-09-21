"""Descriptive steady-tone EQ fit; does not identify a unique dynamic model."""
import json
import numpy as np
from scipy import signal,optimize
from p821_identification_analysis import ROOT,SR,read,linear_kernel,db
from p821_slow_detector import gain_signal
from p821_operating_fit import peak


def shelf(f,q,g):
    A=10**(g/40);o=2*np.pi*f/SR;c=np.cos(o);beta=np.sqrt(A)*np.sin(o)/q
    b=A*np.array([(A+1)-(A-1)*c+beta,2*((A-1)-(A+1)*c),(A+1)-(A-1)*c-beta])
    a=np.array([(A+1)+(A-1)*c+beta,-2*((A-1)+(A+1)*c),(A+1)+(A-1)*c-beta])
    return b/a[0],a/a[0]


def measured():
    h=linear_kernel();records=[]
    for name in ['training-grid','saturation-map']:
        x=read('input-'+name);y=read(name);g=gain_signal(x)
        for c in json.loads((ROOT/(name+'-cases.json')).read_text()):
            if c['mode']!='mono' or c['peak']!=.7:continue
            a=c['start']+round(.7*SR);b=c['end']-128;t=np.arange(a,b)/SR;f=c['frequency']
            A=np.column_stack([np.sin(2*np.pi*f*t),np.cos(2*np.pi*f*t),np.ones(len(t))])
            X=np.linalg.lstsq(A,x[a:b,0],rcond=None)[0];Y=np.linalg.lstsq(A,y[a:b,0],rcond=None)[0]
            H=np.sum((h[0,0]+h[0,1])*np.exp(-2j*np.pi*f*np.arange(h.shape[-1])/SR))
            ratio=(Y[0]+1j*Y[1])/(X[0]+1j*X[1])/H/g[a:b].mean()
            records.append(dict(source=name,frequency=f,relative_real=float(ratio.real),relative_imag=float(ratio.imag)))
    return records


def main():
    records=measured();hz=np.array([r['frequency'] for r in records]);target=np.array([complex(r['relative_real'],r['relative_imag']) for r in records]);rows=[]
    for kinds,centers in [(['peak']*3,[180,2700,10000]),(['shelf','peak','peak','peak'],[60,180,2700,10000])]:
        def response(p):
            H=np.full(len(hz),10**(p[0]/20),dtype=complex)
            for kind,(f,q,g) in zip(kinds,p[1:].reshape(-1,3)):
                b,a=(shelf if kind=='shelf' else peak)(np.exp(f),np.exp(q),g)
                H*=signal.freqz(b,a,worN=hz,fs=SR)[1]
            return H
        def error(p):
            e=(response(p)-target)/abs(target)
            return np.concatenate([e.real,e.imag])
        best=None
        for q0 in [.5,1,2]:
            p=np.array([0.]+[v for f in centers for v in [np.log(f),np.log(q0),4 if f<500 else -4]])
            lo=np.array([-12.]+[v for f in centers for v in [np.log(max(15,f/3)),np.log(.2),-24]])
            hi=np.array([12.]+[v for f in centers for v in [np.log(min(22000,f*3)),np.log(8),24]])
            result=optimize.least_squares(error,p,bounds=(lo,hi),max_nfev=3000,ftol=1e-11,xtol=1e-11,gtol=1e-11)
            if best is None or result.cost<best.cost:best=result
        row=dict(kinds=kinds,target_informed_relative_complex_error_db=db(np.sqrt(np.mean(error(best.x)**2)*2)),gain_db=float(best.x[0]),filters=[dict(kind=kind,frequency=float(np.exp(f)),q=float(np.exp(q)),gain_db=float(g)) for kind,(f,q,g) in zip(kinds,best.x[1:].reshape(-1,3))])
        rows.append(row);print(json.dumps(row),flush=True)
    (ROOT/'steady-eq-description.json').write_text(json.dumps(dict(warning='Steady response description only, not a model validation score',measurements=records,fits=rows),indent=2))


if __name__=='__main__':main()
