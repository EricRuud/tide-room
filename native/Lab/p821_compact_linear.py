"""Stable modal-tail approximation of the measured quiet linear kernel.

Fit only the existing impulse response. A short FIR retains its head exactly;
stable damped sinusoidal modes approximate the remaining tail. This is a
Prony-inspired compact implementation, not identification of the real topology.
No musical waveform or final holdout supplies fitting data.
"""
import argparse,json,time
import numpy as np
from scipy import linalg,optimize,signal
from p821_identification_analysis import ROOT,SR,linear_kernel,metrics


def basis(parameters,n):
    damping,frequency=np.exp(parameters.reshape(-1,2)).T
    t=n/SR;phase=2*np.pi*t[:,None]*frequency[None,:]
    decay=np.exp(-t[:,None]*damping[None,:])
    return np.stack([decay*np.cos(phase),decay*np.sin(phase)],axis=2).reshape(len(n),-1)


def sos(parameters,coefficients):
    result=[]
    for (d,f),(C,D) in zip(np.exp(parameters.reshape(-1,2)),coefficients.reshape(-1,2)):
        radius=np.exp(-d/SR);omega=2*np.pi*f/SR
        result.append([C,radius*(D*np.sin(omega)-C*np.cos(omega)),0.,1.,-2*radius*np.cos(omega),radius*radius])
    return np.array(result)


def main(dc_constrained=False):
    h=linear_kernel();target=h[0].sum(axis=0);w=h[:,:,0]/target[0]
    initial=np.array([[22.,3.5],[70.,18.],[42.,46.],[500.,150.]])
    rows=[];started=time.monotonic();frequencies=np.unique(np.r_[np.geomspace(1,23900,1000),np.linspace(20,23900,1000)])
    target_response=signal.freqz(target,worN=frequencies,fs=SR)[1]
    for head in [256,512,1024,2048]:
        for count in [2,3,4]:
            # Sample the full measured tail, keeping more of the early detail.
            n=np.unique(np.r_[np.arange(head,min(head+2048,len(target)),4),np.arange(head,len(target),16)])
            y=target[n];scale=np.sqrt(np.mean(y*y))
            start=initial[:count].reshape(-1).copy()
            if count==2:start=np.array([[45.,5.],[90.,30.]]).reshape(-1)
            bounds=(np.tile(np.log([1.,.05]),count),np.tile(np.log([2000.,400.]),count))
            def solve(parameters):
                B=basis(parameters,n)
                if dc_constrained:
                    damping,frequency=np.exp(parameters.reshape(-1,2)).T
                    poles=np.exp((-damping+2j*np.pi*frequency)/SR)
                    tail=poles**head/(1-poles);constraint=np.stack([tail.real,tail.imag],axis=1).reshape(-1)
                    target_dc=target[head:].sum();particular=constraint*(target_dc/(constraint@constraint))
                    null=linalg.null_space(constraint[None])
                    c=particular+null@np.linalg.lstsq(B@null,y-B@particular,rcond=1e-10)[0]
                else:c=np.linalg.lstsq(B,y,rcond=1e-10)[0]
                return c,B@c
            def objective(parameters):return (solve(parameters)[1]-y)/scale
            fit=optimize.least_squares(objective,np.log(start),bounds=bounds,max_nfev=1000,ftol=1e-10,xtol=1e-10,gtol=1e-10)
            c,_=solve(fit.x);sections=sos(fit.x,c)
            modal=basis(fit.x,np.arange(len(target)))@c;fir=target[:head]-modal[:head]
            response=signal.freqz(fir,worN=frequencies,fs=SR)[1]
            for section in sections:response+=signal.freqz(section[:3],section[3:],worN=frequencies,fs=SR)[1]
            use=(frequencies>=20)&(frequencies<=22000);relative=(response-target_response)/np.maximum(abs(target_response),.01)
            impulse=modal.copy();impulse[:head]+=fir
            dc=float(fir.sum()+sum(s[:3].sum()/s[3:].sum() for s in sections))
            row=dict(head_taps=head,modes=count,fit_evaluations=fit.nfev,seconds=time.monotonic()-started,
                     impulse=metrics(target,impulse),complex_response_rms_dbr=float(20*np.log10(np.sqrt(np.mean(abs(relative[use])**2)))),
                     complex_response_max_dbr=float(20*np.log10(np.max(abs(relative[use])))),
                     dc_gain=dc,target_dc_gain=float(target.sum()),stable=bool(all(np.max(abs(np.roots(s[3:])))<1 for s in sections)),
                     damping_and_frequency=np.exp(fit.x.reshape(-1,2)).tolist())
            assert row['stable'];rows.append(row)
            suffix='-dc' if dc_constrained else ''
            np.savez(ROOT/f'compact-linear-head{head}-modes{count}{suffix}.npz',fir=fir,parallel_sos=sections,width_matrix=w)
            (ROOT/f'compact-linear-fit{suffix}.json').write_text(json.dumps(dict(fitted_to='impulse-full only',final_holdouts_used=False,dc_constrained=dc_constrained,
                factorization_error_dbr=float(20*np.log10(np.linalg.norm(h-w[:,:,None]*target)/np.linalg.norm(h))),rows=rows),indent=2))
            print(head,count,round(row['impulse']['residual_dbr'],2),round(row['complex_response_rms_dbr'],2),round(row['complex_response_max_dbr'],2),'DC',round(dc,7),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--dc-constrained',action='store_true');main(parser.parse_args().dc_constrained)
