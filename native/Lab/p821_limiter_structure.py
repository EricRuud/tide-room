"""Joint stationary limiter structure fit, explicitly target-informed.

Nuisance amplitudes are not measured internal gains. Harmonic descriptors test
whether one envelope law can explain several frequencies and operating levels.
They are not emulation null scores and do not read final musical holdouts.
"""
import json
import numpy as np
from numba import njit
from scipy import optimize
from p821_identification_analysis import ROOT,SR,read,linear_kernel,db


@njit(cache=True)
def process(x,ratio,attack,release,power):
    out=np.empty_like(x);aa=np.exp(-1/(SR*attack));ar=np.exp(-1/(SR*release));env=0.
    for n in range(len(x)):
        v=x[n]*ratio;absolute=abs(v)**power;alpha=aa if absolute>env else ar
        env=alpha*env+(1-alpha)*absolute
        out[n]=min(1.,max(-1.,v/max(1.,env**(1/power))))
    return out


def main():
    h=linear_kernel();w=h[:,:,0]/(h[0,0,0]+h[0,1,0]);y=read('coherent-clip')@np.linalg.inv(w).T
    cases=[c for c in json.loads((ROOT/'coherent-clip-cases.json').read_text()) if c['mode']=='left' and c['frequency']<=375]
    data=[];orders=np.arange(1,32)
    for c in cases:
        f=c['frequency'];period=round(SR/f);n=period*96;use=np.arange(n-period*8,n)
        phase=2*np.pi*f*use[:,None]/SR*orders;A=np.concatenate([np.sin(phase),np.cos(phase),np.ones((len(use),1))],axis=1);inverse=np.linalg.pinv(A)
        a=c['start']+144*256;yy=y[a:a+len(use),0];cap=float(np.max(abs(yy)))
        phase=2*np.pi*f*np.arange(a,a+len(use))[:,None]/SR*orders;A=np.concatenate([np.sin(phase),np.cos(phase),np.ones((len(use),1))],axis=1)
        co=np.linalg.lstsq(A,yy/cap,rcond=None)[0];z=co[:31]+1j*co[31:62];target=z*np.exp(-1j*orders*np.angle(z[0]))
        data.append(dict(x=np.sin(2*np.pi*f*np.arange(n)/SR),use=use,inverse=inverse,target=target,cap=cap))
    results=[]
    for kind in ['absolute','power']:
        def predict(p,i):
            power=np.exp(p[2]) if kind=='power' else 1.;ratios=p[3:] if kind=='power' else p[2:]
            d=data[i];out=process(d['x'],np.exp(ratios[i]),np.exp(p[0]),np.exp(p[1]),power)[d['use']]
            co=d['inverse']@out;z=co[:31]+1j*co[31:62]
            return z*np.exp(-1j*orders*np.angle(z[0]))
        def error(p):
            e=np.concatenate([predict(p,i)-d['target'] for i,d in enumerate(data)])
            return np.concatenate([e.real,e.imag])
        lower=[1e-6,1e-5]+([.25] if kind=='power' else [])+[1.00001]*4
        upper=[.001,.05]+([8.] if kind=='power' else [])+[30.]*4
        fit=None
        for ratios in [[2.]*4,[1.1]*4,[2.,1.25,1.075,1.1]]:
            initial=[1e-5,.0045]+([1.] if kind=='power' else [])+ratios
            attempt=optimize.least_squares(error,np.log(initial),bounds=(np.log(lower),np.log(upper)),max_nfev=300,ftol=1e-10,xtol=1e-10,gtol=1e-10)
            if fit is None or attempt.cost<fit.cost:fit=attempt
        row=dict(kind=kind,attack_seconds=float(np.exp(fit.x[0])),release_seconds=float(np.exp(fit.x[1])),power=float(np.exp(fit.x[2])) if kind=='power' else 1.,nuisance_ratios=np.exp(fit.x[3:] if kind=='power' else fit.x[2:]).tolist(),target_informed=True,cases=[])
        for i,c in enumerate(cases):
            pred=predict(fit.x,i);target=data[i]['target']
            row['cases'].append(c|dict(descriptor_error_dbr=db(np.linalg.norm(pred-target)/np.linalg.norm(target)),reference_h3_phase=float(np.degrees(np.angle(target[2]))),model_h3_phase=float(np.degrees(np.angle(pred[2]))),reference_h3_dbr=db(abs(target[2]/target[0])),model_h3_dbr=db(abs(pred[2]/pred[0]))))
        results.append(row);print(json.dumps(row),flush=True)
        (ROOT/'limiter-structure.json').write_text(json.dumps(results,indent=2))


if __name__=='__main__':main()
