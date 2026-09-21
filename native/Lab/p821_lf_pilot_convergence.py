"""Find an empirically repeatable central-difference pilot amplitude range."""
import argparse, json
import numpy as np
import soundfile as sf
from p821_lf_experiments import ROOT, SR, N, BLOCK, save, submit, save_json
from p821_identification_analysis import metrics, linear_kernel


def capture():
    n=896*BLOCK;a=256*BLOCK;b=448*BLOCK;t=np.arange(n)/N
    carrier=np.zeros(n);carrier[a:b]=.4*np.sin(2*np.pi*8*t[a:b])
    carrier[a:a+24]*=np.linspace(0,1,24);carrier[b-24:b]*=np.linspace(1,0,24)
    cases=[]
    for k in [7,9]:
        for amp in [.0006,.0012,.0024,.0048]:
            name=f'pilot-convergence-{k:03d}-{round(amp*1e6):04d}'
            pilot=amp*np.cos(2*np.pi*k*t+.317)
            cases.append(dict(name=name,pilot_bin=k,pilot_amplitude=amp,carrier_bin=8,carrier_peak=.4,attack=a,release=b,length=n))
            save_json(ROOT/'pilot-convergence-cases.json',cases)
            for sign,label in [(1,'plus'),(-1,'minus')]:submit(name+'-'+label,save(name+'-'+label,(carrier+sign*pilot)[:,None]*[1,1]))
            if amp==.0024:submit(name+'-repeat',ROOT/f'input-{name}-plus.wav')


def analyze():
    cases=json.loads((ROOT/'pilot-convergence-cases.json').read_text());assert len(cases)==8
    rows=[];previous={};a=11*N;b=14*N;kernel=linear_kernel();kernel=kernel[0,0]+kernel[0,1]
    for case in cases:
        name=case['name'];k=case['pilot_bin'];amp=case['pilot_amplitude']
        yp,_=sf.read(ROOT/f'{name}-plus.wav',always_2d=True);ym,_=sf.read(ROOT/f'{name}-minus.wav',always_2d=True)
        marginal=.5*(yp-ym);normalized=marginal/amp;Y=np.fft.rfft(normalized[a:b,0]);idx=3*k
        # Exactly three complete periods, each beginning at the same pilot phase.
        H=Y[idx]/((b-a)*.5*np.exp(.317j));translated=sorted({abs(k+8*j) for j in range(-10,11)}-{k,0})
        quiet=np.sum(kernel*np.exp(-2j*np.pi*k*np.arange(len(kernel))/N));relative=H/quiet
        row=case|dict(gain_db=float(20*np.log10(abs(H))),phase_deg=float(np.angle(H,deg=True)),
          gain_relative_to_quiet_db=float(20*np.log10(abs(relative))),phase_relative_to_quiet_deg=float(np.angle(relative,deg=True)),
          translated_frequencies_hz=[j*SR/N for j in translated],
          translated_amplitude_vs_input_db=[float(20*np.log10(max(abs(Y[3*j])/((b-a)*.5),1e-15))) for j in translated])
        if k in previous:row['against_previous_half_amplitude']=metrics(normalized[a:b],previous[k][a:b])
        previous[k]=normalized
        if amp==.0024:
            rep,_=sf.read(ROOT/f'{name}-repeat.wav',always_2d=True)
            row['repeat_noise_relative_to_marginal']=metrics(marginal[a:b],marginal[a:b]+.5*(rep[a:b]-yp[a:b]))
        rows.append(row);print(name,'gain',row['gain_db'],'phase',row['phase_deg'],'half comparison',row.get('against_previous_half_amplitude',{}).get('residual_dbr'),flush=True)
    save_json(ROOT/'pilot-convergence-analysis.json',dict(rows=rows,no_fitting=True,final_used=False,
      scope='Local finite difference around one periodic bass carrier. Translated responses are not an LTI filter transfer function.'))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('mode',choices=['capture','analyze']);globals()[p.parse_args().mode]()
