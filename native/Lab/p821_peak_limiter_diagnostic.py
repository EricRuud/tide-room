"""Test whether instantaneous attack with recovery explains clipped harmonic phase.

This fits target-informed stationary shape descriptors, not input/output prediction.
The unknown pre-limiter tone amplitude and phase are nuisance parameters.
"""
import json,time
import numpy as np
from numba import njit
from scipy import optimize
from p821_identification_analysis import ROOT,SR,read,linear_kernel,db


@njit(cache=True)
def limit(x,ratio,tau,kind):
    out=np.empty_like(x);alpha=np.exp(-1/(SR*tau));state=0. if kind==0 else 1.
    for n in range(len(x)):
        value=x[n]*ratio;absolute=abs(value)
        if kind==0:
            state=max(absolute,alpha*state);gain=min(1.,1/max(state,1e-20))
        else:
            state=min(1-alpha*(1-state),1/max(absolute,1e-20));gain=state
        out[n]=value*gain
    return out


@njit(cache=True)
def attack_release(x,ratio,attack,release):
    out=np.empty_like(x);aa=np.exp(-1/(SR*attack));ar=np.exp(-1/(SR*release));env=0.
    for n in range(len(x)):
        value=x[n]*ratio;absolute=abs(value);alpha=aa if absolute>env else ar
        env=alpha*env+(1-alpha)*absolute
        out[n]=min(1.,max(-1.,value/max(1.,env)))
    return out


def coefficients(y,f,start=0):
    t=(np.arange(len(y))+start)/SR;orders=np.arange(1,10);phase=2*np.pi*f*t[:,None]*orders
    A=np.concatenate([np.sin(phase),np.cos(phase),np.ones((len(t),1))],axis=1);co=np.linalg.lstsq(A,y,rcond=None)[0];z=co[:9]+1j*co[9:18]
    return z*np.exp(-1j*orders*np.angle(z[0]))


def main():
    h=linear_kernel();w=h[:,:,0]/(h[0,0,0]+h[0,1,0]);y=read('coherent-clip')@np.linalg.inv(w).T;cases=json.loads((ROOT/'coherent-clip-cases.json').read_text());rows=[]
    for c in cases:
        if c['mode']!='left' or c['frequency']>375:continue
        f=c['frequency'];a=c['start']+144*256;b=c['end']-24*256;cap=np.max(abs(y[a:b,0]));target=coefficients(y[a:b,0]/cap,f,a)
        n=SR//2;t=np.arange(n)/SR;x=np.sin(2*np.pi*f*t);use=np.arange(n-256*48,n)
        orders=np.arange(1,10);phase=2*np.pi*f*use[:,None]/SR*orders
        A=np.concatenate([np.sin(phase),np.cos(phase),np.ones((len(use),1))],axis=1);inverse=np.linalg.pinv(A)
        def predict(p,kind):
            if kind==2:out=attack_release(x,np.exp(p[0]),np.exp(p[1]),np.exp(p[2]))[use]
            else:out=limit(x,np.exp(p[0]),np.exp(p[1]),kind)[use]
            co=inverse@out;z=co[:9]+1j*co[9:18]
            return z*np.exp(-1j*orders*np.angle(z[0]))
        for kind in [0,1,2]:
            def error(p):
                e=predict(p,kind)-target
                return np.concatenate([e.real,e.imag])
            best=None
            starts=[[2,tau,.02] for tau in [.00005,.0005,.002,.01]] if kind==2 else [[1.3,tau] for tau in [.0002,.001,.005,.02]]
            bounds=(np.log([1.00001,.000001,.00001]),np.log([20,.03,.2])) if kind==2 else (np.log([1.00001,.000001]),np.log([20,.2]))
            for initial in starts:
                result=optimize.least_squares(error,np.log(initial),bounds=bounds,max_nfev=500,ftol=1e-11,xtol=1e-11,gtol=1e-11)
                if best is None or result.cost<best.cost:best=result
            yp=predict(best.x,kind)
            row=c|dict(kind=['envelope_decay','gain_recovery','attack_release'][kind],input_to_ceiling_ratio=float(np.exp(best.x[0])),release_seconds=float(np.exp(best.x[-1])),target_informed_harmonic_descriptor_error_dbr=db(np.linalg.norm(yp-target)/np.linalg.norm(target)),target_h3_phase=float(np.degrees(np.angle(target[2]))),model_h3_phase=float(np.degrees(np.angle(yp[2]))),target_h3_dbr=db(abs(target[2]/target[0])),model_h3_dbr=db(abs(yp[2]/yp[0])))
            if kind==2:row['attack_seconds']=float(np.exp(best.x[1]))
            rows.append(row);print(row,flush=True)
    (ROOT/'peak-limiter-diagnostic.json').write_text(json.dumps(rows,indent=2))


if __name__=='__main__':main()
