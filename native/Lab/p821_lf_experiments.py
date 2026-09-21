"""Second-round low-frequency identification, separate from the frozen v31 study.

One analysis thread, short file batches, floating-point capture, no device output.
The first study's final recordings are completed evaluation data, not new holdouts.
"""
import os
for key in ['OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS','VECLIB_MAXIMUM_THREADS']:
    os.environ[key]='1'
import argparse,hashlib,json,time
from pathlib import Path
import numpy as np
import soundfile as sf
import torch
from scipy import signal
import p821_measure as bench
from p821_identification_analysis import ROOT as OLD,SR,metrics,linear_kernel,linear_render
from p821_separated_model import save_json

ROOT=OLD.parent/'p821-identification-002';N=8192;BLOCK=256
bench.ROOT=ROOT
CHECKPOINT=OLD/'surrogate-v31rest-continuous-selected.pt'


def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(name,x):
    path=ROOT/f'input-{name}.wav'
    if not path.exists():sf.write(path,x,SR,subtype='FLOAT')
    return path


def submit(name,source,**options):return bench.submit(name,source,{'Stage Focus':1},warmupInput=False,**options)


def shape(fbin,kind,n):
    t=np.arange(n)/N
    if kind=='sine':return np.sqrt(2)*np.sin(2*np.pi*fbin*t)
    harmonic=np.array([1,3,5,7,9]);weight=1/harmonic
    phase=np.zeros(5) if kind=='aligned' else -np.pi*np.arange(5)*(np.arange(5)-1)/5
    x=(weight[:,None]*np.cos(2*np.pi*fbin*harmonic[:,None]*t+phase[:,None])).sum(axis=0)
    return x/np.sqrt(.5*np.sum(weight**2))


def plan():
    ROOT.mkdir(exist_ok=True);path=ROOT/'experiment-plan.json'
    if path.exists():return
    save_json(path,dict(stage='low-frequency mechanism identification',created_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),
      frozen_parent_checkpoint=str(CHECKPOINT),frozen_parent_sha256=digest(CHECKPOINT),
      sample_rate=SR,block=BLOCK,settings='Same P821 1.5.0 fixed setting as round 1; optional effects off',
      resource_policy='One analysis thread; one capture job at a time; .2 second pauses between analyzed batches; hidden host with no device output. OS scheduling-priority changes unavailable in sandbox.',
      training_or_exploratory=['tone-*','probe-*'],
      experiments=['fundamental gain/phase versus RMS and frequency','same spectrum/RMS with different harmonic phases/crest','central-difference small-signal response around a bass conditioner'],
      tone_bins=[4,8,12,16,24,48,96],tone_rms=[.04,.12,.25,.4],waveforms=['sine','aligned','schroeder'],
      probe_carrier_bins=[8,24],probe_carrier_peaks=[.4,.8],probe_waveforms=['sine','aligned'],
      pilot_bins=[3,5,7,11,17,23,37,61,101,173,293,523,1043,1667,2389,3067],pilot_amplitude=.00015,
      new_validation='Separate generated LF-rich dynamic signals with different frequencies, phases and envelopes; define before fitting a new candidate',
      round1_final_status='Previously evaluated. Never claim these are untouched for future model tuning.',
      inference='Input only; target-informed local response descriptions are diagnostics, not predictive emulation scores',
      sources=['https://arxiv.org/pdf/1804.09587']))


def smoke():
    plan();source=OLD/'input-long-memory-1-conditioned.wav'
    for name,parameters in [('host-parity',{'Stage Focus':1}),('host-bypass',{'Bypass':1})]:
        bench.submit(name,source,parameters,warmupInput=False)
    reference,_=sf.read(OLD/'long-memory-1-conditioned.wav',always_2d=True);actual,_=sf.read(ROOT/'host-parity.wav',always_2d=True)
    x,_=sf.read(source,always_2d=True);bypass,_=sf.read(ROOT/'host-bypass.wav',always_2d=True)
    comparison=metrics(reference,actual);assert comparison['residual_dbr']<-90,comparison
    # The initial equality assertion exposed a fixed 52-sample delay in this
    # plugin's bypass path. Wet capture matches the earlier host without shifting.
    delay=52;assert np.array_equal(x[:-delay],bypass[delay:]) and not np.count_nonzero(bypass[:delay])
    save_json(ROOT/'host-validation.json',dict(parity=comparison,bypass_exact_after_delay=True,bypass_delay_samples=delay,
      bypass_unaligned=metrics(x,bypass),note='Initial unaligned bypass equality check failed; delayed equality is exact. No delay fitted or applied to wet model scores.',
      host_binary_sha256=digest(Path(__file__).resolve().parents[2]/'build-room3/TidePluginBench_artefacts/Release/Tide Plugin Bench.app/Contents/MacOS/Tide Plugin Bench')))
    print('Hidden host parity',comparison['residual_dbr'],'bypass exact with 52-sample delay',flush=True)


def capture_tones():
    plan()
    for fbin in [4,8,12,16,24,48,96]:
        blocks=[];cases=[];offset=0
        for kind in ['sine','aligned','schroeder']:
            for rms in [.04,.12,.25,.4]:
                quiet=256*BLOCK;active=192*BLOCK;x=np.zeros((quiet+active,2));raw=shape(fbin,kind,active)*rms
                envelope=np.ones(active);envelope[:24]=np.linspace(0,1,24);envelope[-24:]=np.linspace(1,0,24)
                x[quiet:]=(raw*envelope)[:,None];blocks.append(x)
                cases.append(dict(bin=fbin,frequency=fbin*SR/N,kind=kind,rms=rms,peak=float(np.max(abs(raw))),crest=float(np.max(abs(raw))/rms),
                  start=offset,attack=offset+quiet,end=offset+len(x),steady_start=offset+quiet+3*N,steady_end=offset+quiet+5*N))
                offset+=len(x)
        blocks.append(np.zeros((256*BLOCK,2)));name=f'tone-{fbin:03d}';save_json(ROOT/(name+'-cases.json'),cases)
        submit(name,save(name,np.concatenate(blocks)))
        if fbin==8:submit(name+'-repeat',ROOT/f'input-{name}.wav')


def capture_probes():
    plan();bins=np.array([3,5,7,11,17,23,37,61,101,173,293,523,1043,1667,2389,3067]);n=896*BLOCK;t=np.arange(n)/N
    rng=np.random.default_rng(2821);phases=rng.uniform(-np.pi,np.pi,len(bins));pilot=(.00015*np.cos(2*np.pi*bins[:,None]*t+phases[:,None])).sum(axis=0)
    cases=[]
    for fbin in [8,24]:
        for peak in [.4,.8]:
            for kind in ['sine','aligned']:
                name=f'probe-{fbin:03d}-{round(peak*10)}-{kind}';carrier=np.zeros(n);a=256*BLOCK;b=448*BLOCK
                wave=shape(fbin,kind,b-a);wave*=peak/np.max(abs(wave));wave[:24]*=np.linspace(0,1,24);wave[-24:]*=np.linspace(1,0,24);carrier[a:b]=wave
                case=dict(name=name,bin=fbin,frequency=fbin*SR/N,kind=kind,peak=peak,attack=a,release=b,length=n,pilot_bins=bins.tolist(),pilot_phases=phases.tolist(),pilot_amplitude=.00015)
                cases.append(case);save_json(ROOT/'probe-cases.json',cases)
                for sign,label in [(1,'plus'),(-1,'minus')]:submit(name+'-'+label,save(name+'-'+label,(carrier+sign*pilot)[:,None]*[1,1]))
    submit('probe-baseline',save('probe-baseline',pilot[:,None]*[1,1]))


def model():
    import p821_rest_state_model as extension
    torch.set_num_threads(1);extension.activate();m=extension.Model();m.load_state_dict(torch.load(CHECKPOINT,weights_only=True)['state_dict']);m.eval()
    return extension,m


def analyze_tones():
    plan();extension,m=model();rows=[];h=linear_kernel();kernel=h[0,0]+h[0,1]
    for fbin in [4,8,12,16,24,48,96]:
        name=f'tone-{fbin:03d}';x,_=sf.read(ROOT/f'input-{name}.wav',always_2d=True);y,_=sf.read(ROOT/f'{name}.wav',always_2d=True)
        d=extension.prepare_source(ROOT/f'input-{name}.wav');prediction=extension.core.predict(m,d);values,_=m.control_parts(d['control'].transpose(0,1))
        for case in json.loads((ROOT/(name+'-cases.json')).read_text()):
            a,b=case['steady_start'],case['steady_end'];X=np.fft.rfft(x[a:b,0]);Y=np.fft.rfft(y[a:b,0]);P=np.fft.rfft(prediction[a:b,0]);idx=2*fbin
            linear=np.sum(kernel*np.exp(-2j*np.pi*case['frequency']*np.arange(len(kernel))/SR));actual=Y[idx]/X[idx]/linear;pred=P[idx]/X[idx]/linear
            row=case|dict(steady=metrics(y[a:b],prediction[a:b]),whole_active=metrics(y[case['attack']:case['end']],prediction[case['attack']:case['end']]),
                 reference_gain_db=float(20*np.log10(abs(actual))),reference_phase_deg=float(np.degrees(np.angle(actual))),
                 predicted_gain_db=float(20*np.log10(abs(pred))),predicted_phase_deg=float(np.degrees(np.angle(pred))),
                 gain_error_db=float(20*np.log10(abs(pred/actual))),phase_error_deg=float(np.degrees(np.angle(pred/actual))),
                 controls=values[0,a//BLOCK:b//BLOCK].mean(dim=0).tolist(),
                 reference_harmonics_dbr=[float(20*np.log10(max(abs(Y[idx*k])/abs(Y[idx]),1e-15))) for k in range(2,10)])
            rows.append(row)
        save_json(ROOT/'tone-analysis-v31rest.json',dict(checkpoint_sha256=digest(CHECKPOINT),rows=rows,no_fitting=True))
        print(name,'worst gain/phase error',max(abs(r['gain_error_db']) for r in rows[-12:]),max(abs(r['phase_error_deg']) for r in rows[-12:]),flush=True)
        del d,x,y,prediction;time.sleep(.2)
    repeat,_=sf.read(ROOT/'tone-008-repeat.wav',always_2d=True);actual,_=sf.read(ROOT/'tone-008.wav',always_2d=True)
    save_json(ROOT/'tone-repeatability.json',metrics(actual[SR:],repeat[SR:]))


def analyze_probes():
    plan();extension,m=model();rows=[];baseline,_=sf.read(ROOT/'probe-baseline.wav',always_2d=True)
    for case in json.loads((ROOT/'probe-cases.json').read_text()):
        targets=[];predictions=[]
        for label in ['plus','minus']:
            name=case['name']+'-'+label;target,_=sf.read(ROOT/(name+'.wav'),always_2d=True);d=extension.prepare_source(ROOT/f'input-{name}.wav')
            targets.append(target);predictions.append(extension.core.predict(m,d));del d
        marginal=(targets[0]-targets[1])*.5;predicted=(predictions[0]-predictions[1])*.5;bins=np.array(case['pilot_bins']);periods=[]
        for a in range(0,len(marginal)-N+1,N):
            b=a+N;B=np.fft.rfft(baseline[a:b,0]);Y=np.fft.rfft(marginal[a:b,0]);P=np.fft.rfft(predicted[a:b,0]);r=Y[bins]/B[bins];p=P[bins]/B[bins]
            periods.append(dict(start=a,end=b,time_after_attack=(a-case['attack'])/SR,reference_real=r.real.tolist(),reference_imag=r.imag.tolist(),predicted_real=p.real.tolist(),predicted_imag=p.imag.tolist()))
        rows.append(case|dict(marginal=metrics(marginal[SR:],predicted[SR:]),periods=periods));save_json(ROOT/'probe-analysis-v31rest.json',dict(rows=rows,checkpoint_sha256=digest(CHECKPOINT),no_fitting=True))
        print(case['name'],'marginal residual',rows[-1]['marginal']['residual_dbr'],flush=True);time.sleep(.2)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['plan','smoke','capture-tones','capture-probes','analyze-tones','analyze-probes']);args=parser.parse_args()
    globals()[args.mode.replace('-','_')]()
