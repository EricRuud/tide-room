"""Freeze round 2 before capturing its reserved procedural evaluation signal.

This is a one-time evaluation, with no gain/EQ/alignment fitting in primary scores.
The reference repeat measures capture consistency; it does not replace/average
the first target. The original round-1 final recordings are not accessed.
"""
import argparse, datetime, json, shutil, subprocess, tempfile
from pathlib import Path
from p821_lf_experiments import ROOT, OLD, SR, CHECKPOINT, digest, save_json, submit
import numpy as np
import soundfile as sf
import torch
from scipy import signal
from p821_identification_analysis import metrics, linear_kernel
from p821_final_evaluation import diagnostics
from p821_lf_native_check import CONFIG, selected
import p821_rest_state_model as parent

PROTOCOL=ROOT/'final-lf-protocol.json'
FOLDER=ROOT/'final-evaluation-lf2'


def stamp():return datetime.datetime.now(datetime.timezone.utc).isoformat()


def freeze():
    assert not PROTOCOL.exists(),'Final protocol already exists; do not overwrite'
    assert not (ROOT/'final-lf-pattern.wav').exists(),'Final reference was already captured'
    plan=json.loads((ROOT/'candidate-protocol.json').read_text())
    assert not plan['final_audio_read'] and not plan['final_reference_captured']
    selection=json.loads((ROOT/'gated-lf-selection.json').read_text())
    chosen=selection['chosen'];assert selection['complete'] and chosen is not None
    assert chosen['mean']<=selection['baseline_new_validation_mean']-.2 and chosen['guard']['passes']
    m,checkpoint=selected();sha=digest(checkpoint)
    port=json.loads((ROOT/'native-lf-check.json').read_text());assert port['checkpoint_sha256']==sha and len(port['rows'])==15
    assert all(r['comparison']['peak_error']<5e-6 for r in port['rows'])
    streams=json.loads((ROOT/'native-lf-stream-check.json').read_text())['rows']
    assert [r['factor'] for r in streams]==[1,4,8]
    assert all(r['cases']==20 and r['max_difference']==0 and r['callback_cpp_allocations']==0 and r['finite'] and r['reset_repeat_exact'] for r in streams)
    memory=json.loads((ROOT/'gated-lf-memory-check.json').read_text());assert memory['checkpoint_sha256']==sha
    assert [r['gap'] for r in memory['rows']]==[1,5,15,60]
    assert all(r['history_effect']['residual_dbr']<-100 for r in memory['rows'])
    disabled=json.loads((ROOT/'native-lf-port-audit.json').read_text());assert disabled['disabled_extension_bit_identical']
    source=json.loads((ROOT/'final-lf-pattern-source.json').read_text());assert source['sha256']==digest(source['input'])
    assert source['seed']==plan['final_seed']
    # Copy executables out of temporary storage before recording their hashes.
    binary_dir=ROOT/'native-lf-bin';binary_dir.mkdir(exist_ok=True)
    binaries={}
    for mode,name in [('native','tide-p821-lf-native'),('quality','tide-p821-lf-quality'),('stream','tide-p821-lf-stream')]:
        target=binary_dir/name;shutil.copy2(Path('/private/tmp')/name,target);binaries[mode]=str(target)
    old_protocol=json.loads((OLD/'final-evaluation-protocol.json').read_text())
    # Verify preservation of the first study, without reading its final audio.
    for path,sha_old in old_protocol['implementation_hashes'].items():assert digest(path)==sha_old,path
    dependencies={**old_protocol['dependency_hashes'],str(CHECKPOINT):digest(CHECKPOINT),str(checkpoint):sha}
    for path in [ROOT/'dynamic-lf-step-200.pt',ROOT/'lf-basis-fit.json',ROOT/'gated-lf-initialization.json',ROOT/'gated-lf-selection.json',ROOT/'candidate-protocol.json',ROOT/'native-lf-configuration.json',ROOT/'native-lf-check.json',ROOT/'native-lf-stream-check.json',ROOT/'gated-lf-memory-check.json',ROOT/'native-lf-port-audit.json']:
        dependencies[str(path)]=digest(path)
    sources=list(Path(__file__).parent.glob('p821_*.py'))+list(Path(__file__).with_name('LF2').glob('*'))
    configurations=list(CONFIG.glob('*.bin'))+list((OLD/'native-model-v31rest').glob('*.bin'))+list((OLD/'native-hq-limiter').glob('*.bin'))
    save_json(PROTOCOL,dict(status='frozen',frozen_utc=stamp(),chosen=chosen,checkpoint=str(checkpoint),checkpoint_sha256=sha,
      baseline_checkpoint=str(CHECKPOINT),baseline_checkpoint_sha256=digest(CHECKPOINT),
      lf_basis=m.lf,gate=dict(threshold=m.threshold,amount=m.amount),binaries=binaries,
      hashes={str(p):digest(p) for p in sources+configurations+[Path(p) for p in binaries.values()] if p.is_file()},dependency_hashes=dependencies,
      linear_kernel_sha256=digest_bytes(linear_kernel().tobytes()),input=source['input'],input_sha256=source['sha256'],input_seed=source['seed'],
      final_audio_read=False,final_reference_captured=False,
      scoring='48 kHz stereo, entire file after first second. Raw waveform residual dBr; no fitted gain, EQ, phase or delay. HQ compensation uses fixed implementation latency only.',
      variants=['parent-native','candidate-native','candidate-HQ4','candidate-HQ8'],
      repeat='One additional identical reference capture to estimate repeatability; first reference remains primary.',
      limitations='One previously reserved procedural pattern. Fixed P821 setting only. No inference of its internal implementation, perceptual equivalence, zero aliasing, or audio-device reliability.',
      compiler='clang++ -std=c++17 -O3 -ffp-contract=off; ARM64; sequential compilation',
      no_reselection_after_final=True))
    print('Frozen round-2 candidate',sha,'before reference capture',flush=True)


def digest_bytes(value):
    import hashlib
    return hashlib.sha256(value).hexdigest()


def verify(protocol):
    for group in ['hashes','dependency_hashes']:
        for path,sha in protocol[group].items():assert digest(path)==sha,path
    assert digest(protocol['input'])==protocol['input_sha256']
    assert digest_bytes(linear_kernel().tobytes())==protocol['linear_kernel_sha256']


def capture():
    protocol=json.loads(PROTOCOL.read_text());assert protocol['status']=='frozen';verify(protocol)
    protocol.update(status='capturing',capture_started_utc=stamp());save_json(PROTOCOL,protocol)
    for name in ['final-lf-pattern','final-lf-pattern-repeat']:submit(name,Path(protocol['input']))
    protocol.update(status='captured',final_reference_captured=True,captured_utc=stamp(),
      reference_hashes={name:digest(ROOT/(name+'.wav')) for name in ['final-lf-pattern','final-lf-pattern-repeat']})
    save_json(PROTOCOL,protocol);print('Reserved reference and repeat captured',flush=True)


def evaluate():
    torch.set_num_threads(1);protocol=json.loads(PROTOCOL.read_text());assert protocol['status']=='captured';verify(protocol)
    for name,sha in protocol['reference_hashes'].items():assert digest(ROOT/(name+'.wav'))==sha
    protocol.update(status='evaluating',final_audio_read=True,final_first_read_utc=stamp());save_json(PROTOCOL,protocol)
    x,rate=sf.read(protocol['input'],always_2d=True);target,tr=sf.read(ROOT/'final-lf-pattern.wav',always_2d=True)
    repeat,rr=sf.read(ROOT/'final-lf-pattern-repeat.wav',always_2d=True);assert rate==tr==rr==SR and x.shape==target.shape==repeat.shape
    n=len(x);variants={'P821-reference':target};rows={};m,_=selected()
    with tempfile.TemporaryDirectory(prefix='p821-lf-final-',dir='/private/tmp') as temp:
        temp=Path(temp);padded=np.pad(x,((0,512+(-n)%256),(0,0))).astype('<f4');padded.tofile(temp/'input.bin')
        for label,factor,config in [('parent-native',1,OLD/'native-model-v31rest'),('candidate-native',1,CONFIG),('candidate-HQ4',4,CONFIG),('candidate-HQ8',8,CONFIG)]:
            command=[protocol['binaries']['native'],str(config)] if factor==1 else [protocol['binaries']['quality'],str(config),str(OLD/'native-hq-limiter'),str(factor)]
            command += [str(temp/'input.bin'),str(temp/'output.bin')]
            timing=json.loads(subprocess.check_output(command,text=True));delay=timing.get('latency_samples',0)
            y=np.fromfile(temp/'output.bin',dtype='<f8').reshape(-1,2)[delay:delay+n];assert y.shape==target.shape and np.isfinite(y).all()
            variants[label]=y;rows[label]=diagnostics(x,target,y,(SR,13*SR))|dict(offline_benchmark=timing,fixed_latency_compensation_samples=delay)
            print(label,rows[label]['after_first_second']['residual_dbr'],flush=True)
        d=parent.prepare_source(protocol['input']);python=parent.core.predict(m,d)
        parity=metrics(python,variants['candidate-native']);assert parity['peak_error']<5e-6;del d,python
    maximum=max(float(np.max(abs(y))) for y in variants.values());gain=min(10**(-12/20),.75/max(maximum,1e-12))
    FOLDER.mkdir(exist_ok=True);files={}
    for label,y in variants.items():
        path=FOLDER/(label+'.wav');sf.write(path,y*gain,SR,subtype='PCM_24');check,rr=sf.read(path,always_2d=True)
        assert rr==SR and check.shape==target.shape and np.isfinite(check).all() and np.max(abs(check))<=.750001
        assert np.max(abs(check-y*gain))<2**-22
        files[label]=dict(file=path.name,sha256=digest(path),peak=float(np.max(abs(check))),subtype=sf.info(path).subtype)
    report=dict(protocol=protocol,rows=rows,repeatability=metrics(target[SR:],repeat[SR:]),native_python_agreement=parity,
      common_playback_gain_db=float(20*np.log10(gain)),files=files,no_subjective_listening_claim=True,
      native_improvement_db=rows['parent-native']['after_first_second']['residual_dbr']-rows['candidate-native']['after_first_second']['residual_dbr'],
      quality_comparison={str(factor):metrics(variants['candidate-native'][SR:],variants[f'candidate-HQ{factor}'][SR:]) for factor in [4,8]})
    save_json(FOLDER/'report.json',report);plot(x,target,variants,report)
    protocol.update(status='evaluated',completed_utc=stamp(),report=str(FOLDER/'report.json'));save_json(PROTOCOL,protocol)
    report['protocol']=protocol;save_json(FOLDER/'report.json',report)
    print('Final improvement dB',report['native_improvement_db'],'repeat residual',report['repeatability']['residual_dbr'],flush=True)


def plot(x,target,variants,report):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig,axes=plt.subplots(2,1,figsize=(10,7),layout='constrained')
    colors={'parent-native':'#c15c31','candidate-native':'#167b88'}
    for name,color in colors.items():
        rows=report['rows'][name]['windows']['rows'];rows=[r for r in rows if not r['below_minus80']]
        axes[0].plot([(r['start']+r['end'])/(2*SR) for r in rows],[r['residual_dbr'] for r in rows],label=name,color=color)
        f,p=signal.welch(variants[name][SR:]-target[SR:],fs=SR,nperseg=16384,axis=0)
        axes[1].semilogx(f[1:],10*np.log10(np.maximum(p[1:].mean(axis=1),1e-24)),label=name,color=color)
    axes[0].set(xlabel='Time (s)',ylabel='Residual relative to reference (dBr)',title='Reserved procedural pattern: raw waveform error, lower is better')
    axes[1].set(xlabel='Frequency (Hz)',ylabel='Residual PSD (dBFS/Hz)',xlim=(15,20000),title='Remaining error spectrum; this does not isolate aliasing')
    for ax in axes:ax.grid(alpha=.2);ax.legend()
    fig.savefig(FOLDER/'comparison.png',dpi=160);plt.close(fig)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('mode',choices=['freeze','capture','evaluate']);globals()[p.parse_args().mode]()
