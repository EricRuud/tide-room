"""Native numerical/stream checks for the selected round-2 research candidate."""
import argparse,json,shutil,subprocess,tempfile,time
from pathlib import Path
import numpy as np
import soundfile as sf
import torch
from p821_lf_gated_train import load
from p821_lf_experiments import ROOT,OLD,SR,CHECKPOINT,digest,save_json
from p821_identification_analysis import metrics
from p821_stateful_render import parts,mix
from p821_multistage_limiter import render as quality
import p821_rest_state_model as parent

CONFIG=ROOT/'native-lf-gated'


def selected():
    torch.set_num_threads(1);selection=json.loads((ROOT/'gated-lf-selection.json').read_text());assert selection['complete'] and selection['chosen'] is not None
    step=selection['chosen']['step'];return load(step),ROOT/f'gated-lf-step-{step}.pt'


def export():
    m,path=selected();original=torch.load(CHECKPOINT,weights_only=True)['state_dict']
    assert all(torch.equal(m.state_dict()[k],v) for k,v in original.items()),'Parent must remain frozen'
    CONFIG.mkdir(exist_ok=True)
    for p in (OLD/'native-model-v31rest').glob('*.bin'):shutil.copyfile(p,CONFIG/p.name)
    with (CONFIG/'lf-correction.bin').open('wb') as f:
        m.lf_head.weight.detach().numpy().astype('<f4').tofile(f);m.lf_head.bias.detach().numpy().astype('<f4').tofile(f)
        np.array([m.threshold,m.amount,m.lf['frequency'],m.lf['q'],m.lf['scale']],dtype='<f8').tofile(f)
    save_json(ROOT/'native-lf-configuration.json',dict(checkpoint=str(path),checkpoint_sha256=digest(path),files={p.name:digest(p) for p in CONFIG.glob('*.bin')},source_hashes={p.name:digest(p) for p in Path(__file__).with_name('LF2').iterdir() if p.is_file()}))
    return m,path


def stress():
    rng=np.random.default_rng(34824);n=14*SR;t=np.arange(n)/SR
    carrier=np.column_stack([np.sin(2*np.pi*46.875*t),np.sin(2*np.pi*93.75*t)])
    amplitude=np.select([t<1,t<3,t<4,t<6,t<7,t<9,t<10,t<12],[0,.45,1.,0,.0000003,.7,0,1.4],default=0)
    x=carrier*amplitude[:,None];x[5*SR:6*SR]=0;x[8*SR:8*SR+128]=[2.,-1.5]
    x[10*SR:12*SR,1]=.9*np.sin(2*np.pi*3000*t[10*SR:12*SR]);x+=rng.normal(size=x.shape)*1e-10
    x[:SR]=0;x[12*SR:]=0;return x.astype('float32')


def check():
    m,path=export();rows=[]
    with tempfile.TemporaryDirectory(prefix='p821-lf-native-',dir='/private/tmp') as temp:
        temp=Path(temp);sf.write(temp/'stress.wav',stress(),SR,subtype='FLOAT')
        sources=[('stress',temp/'stress.wav')]+[(name,ROOT/f'input-{name}.wav') for name in ['validation-rounded','validation-sustain','validation-mixed','tone-008']]
        for name,source in sources:
            x,rate=sf.read(source,always_2d=True);assert rate==SR;length=len(x);padded=np.pad(x,((0,512+(-length)%256),(0,0)))
            sf.write(temp/'padded.wav',padded,SR,subtype='FLOAT');d=parent.prepare_source(temp/'padded.wav');d['x'].astype('<f4').tofile(temp/'input.bin')
            pre,cap=parts(m,d)
            for factor in [1,4,8]:
                if factor==1:
                    expected=parent.core.predict(m,d)[:length];command=['/private/tmp/tide-p821-lf-native',str(CONFIG),str(temp/'input.bin'),str(temp/'output.bin')]
                else:
                    expected=mix(quality(pre,cap,float(m.release.exp()),factor,span=256),d)[:length]
                    command=['/private/tmp/tide-p821-lf-quality',str(CONFIG),str(OLD/'native-hq-limiter'),str(factor),str(temp/'input.bin'),str(temp/'output.bin')]
                timing=json.loads(subprocess.check_output(command,text=True));delay=timing.get('latency_samples',0)
                actual=np.fromfile(temp/'output.bin',dtype='<f8').reshape(-1,2)[delay:delay+length];comparison=metrics(expected,actual)
                assert np.isfinite(actual).all() and comparison['peak_error']<5e-6,comparison
                rows.append(dict(source=name,factor=factor,comparison=comparison,benchmark=timing));print(name,factor,'peak difference',comparison['peak_error'],flush=True)
                save_json(ROOT/'native-lf-check.json',dict(rows=rows,checkpoint_sha256=digest(path),no_playback=True,scope='Port agreement, not P821 accuracy',final_used=False))
            del d,x,padded,pre,cap;time.sleep(.2)
        stress().tofile(temp/'stream-input.bin');streams=[]
        for factor in [1,4,8]:
            command=['/private/tmp/tide-p821-lf-stream',str(CONFIG),str(temp/'stream-input.bin')]
            if factor>1:command += [str(OLD/'native-hq-limiter'),str(factor)]
            result=json.loads(subprocess.check_output(command,text=True));streams.append(dict(factor=factor,**result));print('Stream',factor,result,flush=True)
        save_json(ROOT/'native-lf-stream-check.json',dict(rows=streams,scope='Twenty buffer/layout/reset cases per mode; offline timing, no audio-device deadline guarantee',allocation_scope='C++ new/new[] calls in processing only',final_used=False))


def memory():
    m,path=selected();cases=json.loads((OLD/'long-memory-cases.json').read_text());rows=[]
    for case in cases:
        output=[]
        for kind in ['baseline','conditioned']:
            d=parent.prepare_source(OLD/f"input-long-memory-{case['requested_gap']}-{kind}.wav");y=parent.core.predict(m,d);output.append(y[case['probe_start']:case['probe_end']]);del d,y
        comparison=metrics(output[0],output[1]);assert comparison['residual_dbr']<-100,comparison
        rows.append(dict(gap=case['requested_gap'],history_effect=comparison));print('Gap',case['requested_gap'],comparison['residual_dbr'],flush=True)
    save_json(ROOT/'gated-lf-memory-check.json',dict(rows=rows,checkpoint_sha256=digest(path),final_used=False))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('mode',choices=['export','check','memory']);globals()[p.parse_args().mode]()
