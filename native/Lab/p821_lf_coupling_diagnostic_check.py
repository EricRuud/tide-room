"""Check complex mirror extraction against an analytic filtered cubic system.

Periodic FFT filtering gives exact settled LTI conditions. Varying pilot phase
checks conjugate normalization; varying carrier level checks the fixed-phase
prediction. This generated-array check never reads reference or final audio.
"""
import numpy as np
from p821_lf_experiments import ROOT, N, save_json, digest
from p821_lf_coupling_map import response


def main():
    n=3*N;t=np.arange(n)/N;omega=2*np.pi*np.fft.rfftfreq(n)
    pre=(1-.996)/(1-.996*np.exp(-1j*omega));post=(1-np.exp(-1j*omega))/(1-.995*np.exp(-1j*omega))
    filt=lambda x,h:np.fft.irfft(np.fft.rfft(x)*h,n=n)
    k=7;c=8;mirror=2*c-k;eps=.0024;beta=.7;rows=[];measured=[]
    for amplitude in [.15,.4,.8]:
        for phi in [-.7,.317,1.1]:
            carrier=amplitude*np.sin(2*np.pi*c*t);pilot=eps*np.cos(2*np.pi*k*t+phi)
            outputs=[]
            for sign in [1,-1]:
                z=filt(carrier+sign*pilot,pre);outputs.append(filt(z+beta*z**3,post))
            y=.5*(outputs[0]-outputs[1]);r=response(np.column_stack([pilot,pilot]),np.column_stack([y,y]),dict(carrier_bin=c,pilot_bin=k),0,n)
            gain=amplitude*abs(pre[3*c]);theta=-np.pi/2+np.angle(pre[3*c]);pilot_gain=eps*abs(pre[3*k])
            direct=post[3*k]*pre[3*k]*(1+1.5*beta*gain**2+.75*beta*pilot_gain**2)
            mirrored=post[3*mirror]*np.conj(pre[3*k])*.75*beta*gain**2*np.exp(2j*theta)
            observed=[r[part]['real']+1j*r[part]['imag'] for part in ['direct','mirror_conjugate']]
            errors=[float(abs(actual-expected)/max(abs(expected),1e-30)) for actual,expected in zip(observed,[direct,mirrored])]
            assert max(errors)<1e-9,errors
            rows.append(dict(carrier_peak=amplitude,pilot_phase=phi,direct_relative_error=errors[0],mirror_relative_error=errors[1]));measured.append(observed[1])
    z=np.array(measured);matrix=np.column_stack([z.real,z.imag]);_,s,_=np.linalg.svd(matrix,full_matrices=False);ratio=s[1]/s[0];assert ratio<1e-10
    save_json(ROOT/'coupling-diagnostic-check.json',dict(rows=rows,fixed_phase_second_to_first_singular_value=float(ratio),
      no_recorded_audio_read=True,scope='Extraction and fixed-phase diagnostic on a known filtered cubic; not a proof of P821 model structure.',script_sha256=digest(__file__)))
    print('Analytic filtered-cubic check passes; maximum complex relative error',max(max(r['direct_relative_error'],r['mirror_relative_error']) for r in rows),'fixed phase singular ratio',ratio,flush=True)


if __name__=='__main__':main()
