"""Fit a descriptive EQ to measured incremental responses, not an emulator.

The carrier neighborhood is excluded because detector perturbations can radiate
there. A fitted curve does not prove frozen LTI behavior or internal topology.
"""
import argparse,json
import numpy as np
from scipy import signal,optimize
from p821_identification_analysis import ROOT,SR,db


def peak(f,q,g):
    A=10**(g/40);omega=2*np.pi*f/SR;alpha=np.sin(omega)/(2*q);c=np.cos(omega)
    b=np.array([1+alpha*A,-2*c,1-alpha*A]);a=np.array([1+alpha/A,-2*c,1-alpha/A])
    return b/a[0],a/a[0]


def response(parameters,hz):
    result=np.full(len(hz),10**(parameters[0]/20),dtype=np.complex128)
    for f,q,g in parameters[1:].reshape(-1,3):
        b,a=peak(np.exp(f),np.exp(q),g)
        result*=signal.freqz(b,a,worN=hz,fs=SR)[1]
    return result


def main(large=False):
    suffix='-big' if large else ''
    data=np.load(ROOT/f'operating-ir{suffix}.npz');r=data['responses'];q=data['quiet'][0,0,:r.shape[1]]
    frequency=np.fft.rfftfreq(len(q),1/SR);qh=np.fft.rfft(q)
    rows=[]
    for case,carrier in [(2,187.5),(6,6000)]:
        measured=np.fft.rfft((r[case]+r[case+1])*.5)/qh
        hz=np.geomspace(40,22000,320)
        hz=hz[abs(hz-carrier)>max(180,carrier*.15)]
        target=np.interp(hz,frequency,measured.real)+1j*np.interp(hz,frequency,measured.imag)
        for centers in [[2700],[2700,10000],[180,2700,10000],[80,180,2700,10000]]:
            p=np.array([-.3]+[item for f in centers for item in [np.log(f),np.log(.8),-3 if f>500 else 2]])
            lower=np.array([-12]+[item for _ in centers for item in [np.log(20),np.log(.15),-18]])
            upper=np.array([12]+[item for _ in centers for item in [np.log(23000),np.log(10),18]])
            def error(p):
                e=(response(p,hz)-target)/np.maximum(abs(target),.01)
                return np.concatenate([e.real,e.imag])
            result=optimize.least_squares(error,p,bounds=(lower,upper),max_nfev=4000,ftol=1e-12,xtol=1e-12,gtol=1e-12)
            row=dict(carrier=carrier,peak=.7,sections=len(centers),descriptive_relative_complex_error_db=db(np.sqrt(np.mean(error(result.x)**2)*2)),gain_db=float(result.x[0]),filters=[dict(frequency=float(np.exp(f)),q=float(np.exp(q)),gain_db=float(g)) for f,q,g in result.x[1:].reshape(-1,3)],parameters=result.x.tolist())
            rows.append(row);print(row,flush=True)
    (ROOT/f'operating-ir{suffix}-eq-fits.json').write_text(json.dumps(rows,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--large',action='store_true')
    main(parser.parse_args().large)
