"""Frozen-candidate evaluation; final audio is inaccessible before explicit freeze.

The self-check uses generated arrays only. Selection and fitting never import
or call the final rendering path. Primary scores have no fitted gain or EQ.
"""
import argparse,datetime,hashlib,importlib,json
from pathlib import Path
import numpy as np
from scipy import signal
import soundfile as sf
import torch
from p821_identification_analysis import ROOT,SR,read,metrics
from p821_separated_model import save_json
from p821_select_continuous import MODULES
from p821_stateful_render import parts,mix
from p821_multistage_limiter import render as quality

PROTOCOL=ROOT/'final-evaluation-protocol.json'


def stamp():return datetime.datetime.now(datetime.timezone.utc).isoformat()


def freeze():
    protocol=json.loads(PROTOCOL.read_text());assert protocol['status']=='protocol_fixed_candidate_pending'
    eligible=[]
    for version in ['v18w','v19w','v20','v21','v22','v23','v24','v25','v26','v27','v27ac','v28','v29','v30','v31','v32','v27svf','v33','v31svf','v33svf','v31settled','v31settledall','v31rest','v33rest']:
        path=ROOT/f'selection-{version}-continuous.json'
        if not path.exists():continue
        d=json.loads(path.read_text());assert d['continuous_state'] and d['all_components_recomputed']
        assert d['plan']['weights']==protocol['development_weights']
        final=d['plan'].get('expected_final_step',10000)
        assert [c['step'] for c in d['candidates']]==list(range(0,final+1,500)),f'{version} selection unfinished'
        eligible.append(dict(version=version,**d['selected']))
    for version in ['v29','v30','v31','v32','v27svf','v33','v31svf','v33svf','v31settled','v31settledall','v31rest','v33rest']:assert any(r['version']==version for r in eligible),f'{version} planned experiment not evaluated yet'
    best=min(eligible,key=lambda r:r['score']);path=ROOT/f"surrogate-{best['version']}-continuous-selected.pt"
    saved=torch.load(path,weights_only=True);assert saved['step']==best['step']
    protocol.update(status='frozen',frozen_utc=stamp(),chosen=best,checkpoint=str(path),checkpoint_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                    evaluated_development_candidates=eligible,final_audio_read=False)
    from p821_identification_analysis import linear_kernel
    from p821_width_separated_model import WIDTH_CHECKPOINT
    protocol['linear_kernel_sha256']=hashlib.sha256(linear_kernel().tobytes()).hexdigest()
    dependencies=[ROOT/WIDTH_CHECKPOINT,ROOT/'slow-detector-fit.json']
    if best['version']=='v32':dependencies.append(ROOT/'surrogate-v27-continuous-selected.pt')
    protocol['dependency_hashes']={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in dependencies}
    # A separately disclosed robustness variant can accompany the unchanged
    # primary weighted winner; final audio never participates in choosing it.
    rest_candidates=[]
    for row in eligible:
        version=row['version']
        if not version.endswith('rest') or row['score']>best['score']+.05:continue
        audit_path=ROOT/f'long-memory-audit-{version}.json'
        if not audit_path.exists():continue
        audit=json.loads(audit_path.read_text());checkpoint=ROOT/f'surrogate-{version}-continuous-selected.pt'
        sha=hashlib.sha256(checkpoint.read_bytes()).hexdigest()
        assert audit['checkpoint_hashes'][version]==sha
        assert [r['requested_gap'] for r in audit['rows']]==[1,5,15,60]
        if all(r['models'][version]['history_effect']['residual_dbr']<=-100 for r in audit['rows']):
            rest_candidates.append(row|dict(checkpoint=str(checkpoint),checkpoint_sha256=sha))
    protocol['secondary_rest_candidate']=min(rest_candidates,key=lambda r:r['score']) if rest_candidates else None
    protocol['implementation_hashes']={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in Path(__file__).parent.glob('p821_*.py')}
    save_json(PROTOCOL,protocol);print('Frozen',best['version'],best['step'],'without opening final audio',flush=True)


def transients(x):
    power=np.mean(x*x,axis=1);envelopes=[]
    for tau in [.01,.05]:
        alpha=np.exp(-1/(SR*tau));envelopes.append(np.sqrt(signal.lfilter([1-alpha],[1,-alpha],power)))
    ratio=envelopes[0]/np.maximum(envelopes[1],1e-12);ratio[envelopes[0]<10**(-50/20)]=0
    peaks,_=signal.find_peaks(ratio,height=1.4,distance=int(.08*SR))
    peaks=peaks[np.argsort(ratio[peaks])[-20:]]
    return [dict(sample=int(p),ratio=float(ratio[p]),start=max(0,int(p-.015*SR)),end=min(len(x),int(p+.05*SR))) for p in sorted(peaks)]


def window_metrics(target,prediction):
    rows=[];size=SR//4
    for a in range(SR,len(target),size):
        b=min(a+size,len(target));rms=float(np.sqrt(np.mean(target[a:b]**2)))
        rows.append(dict(start=a,end=b,target_rms_dbfs=float(20*np.log10(max(rms,1e-15))),below_minus80=rms<1e-4,
                         **metrics(target[a:b],prediction[a:b])))
    active=[r['residual_dbr'] for r in rows if not r['below_minus80']]
    return dict(rows=rows,active_window_residual_percentiles=dict(zip(['p10','median','p90','worst'],np.percentile(active,[10,50,90,100]).tolist())) if active else {})


def diagnostics(x,target,prediction,active):
    a,b=active;result=dict(after_first_second=metrics(target[SR:],prediction[SR:]),whole=metrics(target,prediction),active=metrics(target[a:b],prediction[a:b]),
          model_peak=float(np.max(abs(prediction))),nonfinite_count=int(np.sum(~np.isfinite(prediction))),windows=window_metrics(target,prediction))
    result['channels']={label:metrics(target[SR:,c],prediction[SR:,c]) for c,label in enumerate(['left','right'])}
    ms=lambda y:np.column_stack([y.mean(axis=1),(y[:,0]-y[:,1])*.5])
    tm,pm=ms(target),ms(prediction);result['mid_side']={label:metrics(tm[SR:,c],pm[SR:,c]) for c,label in enumerate(['mid','side'])}
    hp=signal.butter(4,[8000,20000],btype='bandpass',fs=SR,output='sos')
    result['hf_residual_not_isolated_aliasing']=metrics(signal.sosfilt(hp,target,axis=0)[SR:],signal.sosfilt(hp,prediction,axis=0)[SR:])
    result['transients']=[]
    for event in transients(x):
        a,b=event['start'],event['end'];result['transients'].append(event|metrics(target[a:b],prediction[a:b])|dict(reference_peak=float(np.max(abs(target[a:b]))),model_peak=float(np.max(abs(prediction[a:b])))))
    return result


def evaluate():
    protocol=json.loads(PROTOCOL.read_text());assert protocol['status']=='frozen','Freeze the completed development winner before opening final recordings'
    checkpoint=Path(protocol['checkpoint']);assert hashlib.sha256(checkpoint.read_bytes()).hexdigest()==protocol['checkpoint_sha256']
    from p821_identification_analysis import linear_kernel
    assert hashlib.sha256(linear_kernel().tobytes()).hexdigest()==protocol['linear_kernel_sha256']
    for path,expected in protocol['dependency_hashes'].items():assert hashlib.sha256(Path(path).read_bytes()).hexdigest()==expected
    for path,expected in protocol['implementation_hashes'].items():assert hashlib.sha256(Path(path).read_bytes()).hexdigest()==expected
    version=protocol['chosen']['version'];extension=importlib.import_module(MODULES[version.removesuffix('w')]);extension.activate();torch.set_num_threads(1)
    if version.endswith('w'):
        from p821_width_separated_model import prepare_source
        extension.core.prepare_source=prepare_source
    model=extension.core.Model();model.load_state_dict(torch.load(checkpoint,weights_only=True)['state_dict']);model.eval()
    primary_prepare=extension.core.prepare_source
    secondary=protocol.get('secondary_rest_candidate');secondary_model=None
    if secondary is not None and secondary['version']!=version:
        path=Path(secondary['checkpoint']);assert hashlib.sha256(path.read_bytes()).hexdigest()==secondary['checkpoint_sha256']
        secondary_extension=importlib.import_module(MODULES[secondary['version']]);secondary_extension.activate()
        secondary_prepare=secondary_extension.core.prepare_source;secondary_model=secondary_extension.core.Model()
        secondary_model.load_state_dict(torch.load(path,weights_only=True)['state_dict']);secondary_model.eval()
    manifest={c['name']:c for c in json.loads((ROOT/'final-holdout-manifest.json').read_text())}
    folder=ROOT/f'final-evaluation-{version}';folder.mkdir(exist_ok=True);rows=[]
    # Mark access before the first final file is read, even if a later job fails.
    protocol.update(final_audio_read=True,recordings_accessed_for_this_protocol=True);protocol.setdefault('final_audio_first_read_utc',stamp());save_json(PROTOCOL,protocol)
    for name in protocol['final_recordings']:
        source=json.loads((ROOT/(name+'.json')).read_text())['job']['input'];d=primary_prepare(source);target=read(name)
        primary=extension.core.predict(model,d);assert len(target)==len(primary)==d['length']
        x=d['x'][:d['length']];case=manifest[name];active=(case['active_start'],case['active_end'])
        row=dict(recording=name,primary=diagnostics(x,target,primary,active));pre,cap=parts(model,d)
        row['limiter_input']=dict(maximum_instantaneous_ceiling_ratio=float(np.max(abs(pre)/cap)),fraction_above_instantaneous_ceiling=float(np.mean(abs(pre)>cap)))
        variants={'P821-reference':target,'candidate-native':primary};row['quality_modes']={}
        release=float(model.release.exp().detach())
        for factor in [4,8]:
            rendered=mix(quality(pre,cap,release,factor,span=256),d);variants[f'candidate-HQ{factor}']=rendered
            row['quality_modes'][str(factor)]=dict(against_reference=metrics(target[SR:],rendered[SR:]),against_native=metrics(primary[SR:],rendered[SR:]),identical_to_native=bool(np.array_equal(primary,rendered)))
        if secondary_model is not None:
            secondary_data=secondary_prepare(source);secondary_output=secondary_extension.core.predict(secondary_model,secondary_data)
            row['secondary_rest_candidate']=dict(version=secondary['version'],diagnostics=diagnostics(x,target,secondary_output,active))
            variants['resting-candidate-native']=secondary_output
            sp,sc=parts(secondary_model,secondary_data)
            row['secondary_rest_candidate']['limiter_input']=dict(maximum_instantaneous_ceiling_ratio=float(np.max(abs(sp)/sc)),fraction_above_instantaneous_ceiling=float(np.mean(abs(sp)>sc)))
            variants['resting-candidate-HQ8']=mix(quality(sp,sc,float(secondary_model.release.exp().detach()),8,span=256),secondary_data)
            sq=variants['resting-candidate-HQ8']
            row['secondary_rest_candidate']['HQ8']=dict(against_reference=metrics(target[SR:],sq[SR:]),against_native=metrics(secondary_output[SR:],sq[SR:]),identical_to_native=bool(np.array_equal(sq,secondary_output)))
        assert all(np.isfinite(y).all() for y in variants.values()),'Do not export nonfinite audition audio'
        maximum=max(float(np.max(abs(y))) for y in variants.values());gain=min(10**(-12/20),.75/max(maximum,1e-12));row['common_playback_gain_db']=float(20*np.log10(gain));row['files']={}
        for label,y in variants.items():
            if label.startswith('candidate-HQ') and np.array_equal(y,primary):row['files'][label]=dict(row['files']['candidate-native'],identical_to='candidate-native');continue
            if label=='resting-candidate-HQ8' and np.array_equal(y,variants['resting-candidate-native']):
                row['files'][label]=dict(row['files']['resting-candidate-native'],identical_to='resting-candidate-native');continue
            path=folder/f'{name}-{label}.wav';sf.write(path,y*gain,SR,subtype='PCM_24');row['files'][label]=dict(file=path.name,sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        rows.append(row);report=dict(protocol=protocol,rows=rows,evaluation_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),no_subjective_listening_claim=True)
        save_json(folder/'report.json',report);print(name,row['primary']['after_first_second']['residual_dbr'],flush=True)
    protocol.update(status='evaluated',completed_utc=stamp(),report=str(folder/'report.json'));save_json(PROTOCOL,protocol)
    report['protocol']=protocol;save_json(folder/'report.json',report)


def check():
    rng=np.random.default_rng(321821);x=rng.normal(size=(SR*2,2))*.1;y=x*.99
    result=diagnostics(x,x,y,(SR,len(x)));assert abs(result['whole']['residual_dbr']+40)<1e-8
    assert result['nonfinite_count']==0 and len(result['windows']['rows'])==4
    zeros=np.zeros_like(x);result=diagnostics(zeros,zeros,zeros,(SR,len(x)));assert result['model_peak']==0 and not result['transients']
    assert not result['windows']['active_window_residual_percentiles']
    print('Generated-array metrics, windows, silence and empty-event checks pass; no final audio accessed',flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['check','freeze','evaluate']);args=parser.parse_args()
    {'check':check,'freeze':freeze,'evaluate':evaluate}[args.mode]()
