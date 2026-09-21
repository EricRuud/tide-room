"""Frequency-weighted stable modal tail fitting, with exact DC constraint.

The measured impulse is the only target. Variable projection solves the mode
amplitudes linearly for each set of stable decay/frequency parameters. Exact
finite-tail transfer formulas avoid subtracting nearly equal long responses.
"""
import json,time
import numpy as np
from scipy import linalg,optimize,signal
from p821_identification_analysis import ROOT,SR,linear_kernel,metrics
from p821_compact_linear import basis,sos


def tail_basis(parameters,head,omega):
    damping,frequency=np.exp(parameters.reshape(-1,2)).T;p=np.exp((-damping+2j*np.pi*frequency)/SR)
    z=np.exp(-1j*omega[:,None]);rotation=np.exp(-1j*omega[:,None]*head)
    positive=rotation*p[None,:]**head/(1-z*p[None,:])
    negative=rotation*np.conj(p)[None,:]**head/(1-z*np.conj(p)[None,:])
    return np.stack([.5*(positive+negative),(positive-negative)/(2j)],axis=2).reshape(len(omega),-1)


def main():
    h=linear_kernel();target=h[0].sum(axis=0);w=h[:,:,0]/target[0]
    previous=json.loads((ROOT/'compact-linear-fit-dc.json').read_text())
    warm=next(row['damping_and_frequency'] for row in previous['rows'] if row['head_taps']==1024 and row['modes']==4)
    initial=np.array(warm+[[500.,150.],[3000.,2700.]])
    frequencies=np.unique(np.r_[np.geomspace(1,23900,600),np.linspace(20,23900,600)]);omega=2*np.pi*frequencies/SR
    full=signal.freqz(target,worN=frequencies,fs=SR)[1];weight=1/np.maximum(abs(full),.02)
    rows=[];started=time.monotonic()
    for head in [64,128,256]:
        tail=signal.freqz(target[head:],worN=frequencies,fs=SR)[1]*np.exp(-1j*omega*head)
        y=np.r_[(tail*weight).real,(tail*weight).imag];dc=target[head:].sum()
        for count in [4,5,6]:
            def solve(parameters):
                complex_basis=tail_basis(parameters,head,omega)*weight[:,None]
                B=np.r_[complex_basis.real,complex_basis.imag]
                constraint=tail_basis(parameters,head,np.array([0.]))[0].real
                scale=np.maximum(np.linalg.norm(B,axis=0),1e-30);B=B/scale;constraint=constraint/scale
                particular=constraint*dc/(constraint@constraint);null=linalg.null_space(constraint[None])
                c=particular+null@np.linalg.lstsq(B@null,y-B@particular,rcond=1e-10)[0]
                return c/scale,B@c
            def objective(parameters):return solve(parameters)[1]-y
            fit=optimize.least_squares(objective,np.log(initial[:count]).reshape(-1),
                  bounds=(np.tile(np.log([1.,.02]),count),np.tile(np.log([30000.,20000.]),count)),
                  max_nfev=1500,ftol=1e-11,xtol=1e-11,gtol=1e-11)
            c,_=solve(fit.x);sections=sos(fit.x,c);modal=basis(fit.x,np.arange(len(target)))@c
            fir=target[:head]-modal[:head];pred=full-tail+tail_basis(fit.x,head,omega)@c
            error=(pred-full)*weight;use=(frequencies>=20)&(frequencies<=22000)
            impulse=modal.copy();impulse[:head]+=fir
            actual_dc=float(fir.sum()+sum(s[:3].sum()/s[3:].sum() for s in sections))
            row=dict(head_taps=head,modes=count,evaluations=fit.nfev,seconds=time.monotonic()-started,
                     impulse=metrics(target,impulse),complex_response_rms_dbr=float(20*np.log10(np.sqrt(np.mean(abs(error[use])**2)))),
                     complex_response_max_dbr=float(20*np.log10(np.max(abs(error[use])))),dc_gain=actual_dc,
                     target_dc_gain=float(target.sum()),maximum_modal_amplitude=float(np.max(abs(c))),
                     damping_and_frequency=np.exp(fit.x.reshape(-1,2)).tolist())
            rows.append(row);np.savez(ROOT/f'compact-frequency-head{head}-modes{count}.npz',fir=fir,parallel_sos=sections,width_matrix=w)
            (ROOT/'compact-linear-frequency-fit.json').write_text(json.dumps(dict(fitted_to='impulse-full only',
                exact_DC_constraint=True,final_holdouts_used=False,rows=rows),indent=2))
            print(head,count,round(row['complex_response_rms_dbr'],2),round(row['complex_response_max_dbr'],2),
                  'IR',round(row['impulse']['residual_dbr'],2),'DC',round(actual_dc,8),'seconds',round(row['seconds'],1),flush=True)


if __name__=='__main__':main()
