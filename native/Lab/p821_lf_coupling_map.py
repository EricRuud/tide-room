"""Post-freeze exploratory frequency-coupling map; no fitting or selection.

One isolated pilot on either side of a periodic carrier, central differences,
and spot convergence/repeat checks. Reuse the verified 46.875 Hz captures.
"""
import argparse, json, subprocess, tempfile, time
from pathlib import Path
import numpy as np
import soundfile as sf
from p821_lf_experiments import ROOT, OLD, SR, N, BLOCK, save, submit, save_json, digest
from p821_identification_analysis import metrics, linear_kernel


def capture():
    protocol=json.loads((ROOT/'final-lf-protocol.json').read_text());assert protocol['status']=='evaluated'
    plan=dict(carrier_bins=[6,8,10,12],carrier_peaks=[.15,.4,.8],pilot_offsets=[-1,1],pilot_peak=.0024,
      plateau_periods=[11,14],carrier_active_periods=[8,14],phase=.317,
      spot_checks=[dict(carrier_bin=6,carrier_peak=.15,pilot_offset=-1),dict(carrier_bin=12,carrier_peak=.8,pilot_offset=1)],
      no_fitting=True,post_final_exploration=True,final_result_does_not_change=True)
    save_json(ROOT/'coupling-map-plan.json',plan);cases=[];n=896*BLOCK;t=np.arange(n)/N;a=8*N;b=14*N
    for carrier_bin in plan['carrier_bins']:
        for peak in plan['carrier_peaks']:
            carrier=np.zeros(n);carrier[a:b]=peak*np.sin(2*np.pi*carrier_bin*t[a:b]);carrier[a:a+24]*=np.linspace(0,1,24);carrier[b-24:b]*=np.linspace(1,0,24)
            for offset in plan['pilot_offsets']:
                k=carrier_bin+offset;name=f'coupling-map-{carrier_bin:02d}-{round(100*peak):02d}-{k:02d}'
                reused=carrier_bin==8 and peak==.4
                if reused:name=f'pilot-convergence-{k:03d}-2400'
                check=dict(carrier_bin=carrier_bin,carrier_peak=peak,pilot_offset=offset) in plan['spot_checks']
                case=dict(name=name,carrier_bin=carrier_bin,carrier_peak=peak,pilot_bin=k,pilot_amplitude=.0024,reused=reused,spot_check=check)
                cases.append(case);save_json(ROOT/'coupling-map-cases.json',cases)
                if not reused:
                    pilot=.0024*np.cos(2*np.pi*k*t+.317)
                    for sign,label in [(1,'plus'),(-1,'minus')]:submit(name+'-'+label,save(name+'-'+label,(carrier+sign*pilot)[:,None]*[1,1]))
                if check:
                    submit(name+'-repeat',ROOT/f'input-{name}-plus.wav')
                    pilot=.0012*np.cos(2*np.pi*k*t+.317)
                    for sign,label in [(1,'plus'),(-1,'minus')]:submit(name+'-half-'+label,save(name+'-half-'+label,(carrier+sign*pilot)[:,None]*[1,1]))


def response(x,y,case,a=11*N,b=14*N):
    k=case['pilot_bin'];mirror=2*case['carrier_bin']-k;periods=(b-a)//N
    X=np.fft.rfft(x[a:b,0]);Y=np.fft.rfft(y[a:b,0]);direct=Y[periods*k]/X[periods*k];coupled=Y[periods*mirror]/np.conj(X[periods*k])
    value=lambda z:dict(real=float(z.real),imag=float(z.imag),gain_db=float(20*np.log10(max(abs(z),1e-15))),phase_deg=float(np.angle(z,deg=True)))
    return dict(direct=value(direct),mirror_conjugate=value(coupled),mirror_frequency=mirror*SR/N)


def analyze():
    cases=json.loads((ROOT/'coupling-map-cases.json').read_text());assert len(cases)==24
    protocol=json.loads((ROOT/'final-lf-protocol.json').read_text());binary=Path(protocol['binaries']['native']);assert digest(binary)==protocol['hashes'][str(binary)]
    rows=[];a=11*N;b=14*N
    with tempfile.TemporaryDirectory(prefix='p821-lf-coupling-',dir='/private/tmp') as temp:
        temp=Path(temp)
        for case in cases:
            name=case['name'];inputs=[];targets=[]
            for label in ['plus','minus']:
                x,rate=sf.read(ROOT/f'input-{name}-{label}.wav',always_2d=True);assert rate==SR;inputs.append(x)
                y,rate=sf.read(ROOT/f'{name}-{label}.wav',always_2d=True);assert rate==SR;targets.append(y)
            pilot=.5*(inputs[0]-inputs[1]);marginal=.5*(targets[0]-targets[1]);reference=response(pilot,marginal,case)
            row=case|dict(reference=reference,reference_periods=[response(pilot,marginal,case,j*N,(j+1)*N) for j in range(11,14)],models={})
            for version,config in [('parent',OLD/'native-model-v31rest'),('candidate',ROOT/'native-lf-gated')]:
                outputs=[]
                for x in inputs:
                    x.astype('<f4').tofile(temp/'input.bin');subprocess.check_output([str(binary),str(config),str(temp/'input.bin'),str(temp/'output.bin')],text=True)
                    y=np.fromfile(temp/'output.bin',dtype='<f8').reshape(-1,2);assert np.isfinite(y).all();outputs.append(y)
                predicted=.5*(outputs[0]-outputs[1]);row['models'][version]=dict(response=response(pilot,predicted,case),plateau=metrics(marginal[a:b],predicted[a:b]))
            if case['spot_check']:
                repeat,_=sf.read(ROOT/f'{name}-repeat.wav',always_2d=True)
                yp,_=sf.read(ROOT/f'{name}-half-plus.wav',always_2d=True);ym,_=sf.read(ROOT/f'{name}-half-minus.wav',always_2d=True)
                row['half_amplitude_convergence']=metrics(marginal[a:b],(yp-ym)[a:b])
                row['repeat_noise_relative_to_marginal']=metrics(marginal[a:b],marginal[a:b]+.5*(repeat[a:b]-targets[0][a:b]))
                row['half_amplitude_response']=response(pilot,yp-ym,case)
                row['repeat_line_error']=response(pilot,.5*(repeat-targets[0]),case)
            rows.append(row);print(name,[(k,v['plateau']['residual_dbr']) for k,v in row['models'].items()],flush=True)
            save_json(ROOT/'coupling-map-analysis.json',dict(rows=rows,complete=False,post_final_exploration=True,no_fitting=True));time.sleep(.2)
    save_json(ROOT/'coupling-map-analysis.json',dict(rows=rows,complete=True,post_final_exploration=True,no_fitting=True,
      candidate_checkpoint_sha256=protocol['checkpoint_sha256'],script_sha256=digest(__file__),binary_sha256=digest(binary),
      scope='One late 0.512-second window per conditioned probe; not an asymptotic LTI transfer function or a new holdout score.'))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('mode',choices=['capture','analyze']);globals()[p.parse_args().mode]()
