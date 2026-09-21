"""Phase-only conditioner changes: same RMS/spectrum, different crest factor.

Polarity-differenced quiet probes after each conditioner estimate a local response.
A difference can implicate peak-sensitive/history-dependent processing, but filter
ringing plus nonlinearity must still be considered before naming a detector.
"""
import argparse,json
import numpy as np
from p821_identify import ROOT,SR,save,submit
from p821_identification_analysis import read,db


def capture(validation=False):
    name='crest-validation' if validation else 'crest-history'
    n=192*256;cases=[];conditioners=[np.zeros((384*256,2))];probes=[np.zeros((384*256,2))];offset=len(conditioners[0])
    for f in ([375,3000] if validation else [187.5,1500]):
        for level in ([.4,.6] if validation else [.3,.5,.7]):
            for gap in ([16,48] if validation else [32,64]):
                for sign in [1,-1]:
                    c=np.zeros((n,2));p=np.zeros_like(c);a=48*256;b=a+96*256;t=np.arange(b-a)/SR
                    carrier=level*(np.sin(2*np.pi*f*t)+sign*np.sin(6*np.pi*f*t)/3)
                    c[a:b]=carrier[:,None]*[1,.6];pa=b+round(gap*SR/1000);pn=6*256
                    env=np.ones(pn);env[:24]=np.linspace(0,1,24);env[-24:]=np.linspace(1,0,24)
                    p[pa:pa+pn]=(.005*env*np.sin(2*np.pi*8000*np.arange(pn)/SR))[:,None]*[1,.6]
                    cases.append(dict(start=offset,conditioner_start=offset+a,conditioner_end=offset+b,probe_start=offset+pa,probe_end=offset+pa+pn,frequency=f,level=level,gap_ms=gap,harmonic_sign=sign,conditioner_rms=float(np.sqrt(np.mean(carrier**2))),conditioner_peak=float(np.max(abs(carrier)))))
                    conditioners.append(c);probes.append(p);offset+=n
    c=np.concatenate(conditioners);p=np.concatenate(probes)
    (ROOT/(name+'-cases.json')).write_text(json.dumps(cases,indent=2))
    for sign,suffix in [(1,'plus'),(-1,'minus')]:submit(name+'-'+suffix,save(name+'-'+suffix,c+sign*p))


def measure(y,name='crest-history'):
    cases=json.loads((ROOT/(name+'-cases.json')).read_text());rows=[]
    for first,second in zip(cases[::2],cases[1::2]):
        results=[]
        for c in [first,second]:
            a=c['probe_start']+48;b=c['probe_end']-48;t=np.arange(a-c['probe_start'],b-c['probe_start'])/SR
            A=np.column_stack([np.sin(2*np.pi*8000*t),np.cos(2*np.pi*8000*t),np.ones(len(t))]);coef=np.linalg.lstsq(A,y[a:b,0],rcond=None)[0]
            results.append(complex(coef[0],coef[1]))
        ratio=results[1]/results[0]
        row={k:first[k] for k in ['frequency','level','gap_ms']};row.update(crest_peaks=[first['conditioner_peak'],second['conditioner_peak']],equal_rms=[first['conditioner_rms'],second['conditioner_rms']],probe_gain_difference_db=db(abs(ratio)),probe_phase_difference_degrees=float(np.degrees(np.angle(ratio))))
        rows.append(row)
    return rows


def analyze():
    y=(read('crest-history-plus')-read('crest-history-minus'))*.5;rows=measure(y)
    for row in rows:print(row,flush=True)
    (ROOT/'crest-history-analysis.json').write_text(json.dumps(rows,indent=2))


def candidate(version,name='crest-history'):
    import torch
    from p821_identification_analysis import metrics
    if version=='v15':import p821_shelf_control_model as extension
    elif version=='v16':import p821_peak_control_model as extension
    elif version=='v17':import p821_staged_clip_model as extension
    elif version=='v18':import p821_memory_control_model as extension
    extension.activate();core=extension.core;torch.set_num_threads(2)
    checkpoint=torch.load(ROOT/f'surrogate-{version}-best.pt',weights_only=True);model=core.Model();model.load_state_dict(checkpoint['state_dict']);model.eval();outputs=[]
    for suffix in ['plus','minus']:
        job=json.loads((ROOT/(name+'-'+suffix+'.json')).read_text())['job'];d=core.prepare_source(job['input']);outputs.append(core.predict(model,d))
    predicted=(outputs[0]-outputs[1])*.5;target=(read(name+'-plus')-read(name+'-minus'))*.5
    cases=json.loads((ROOT/(name+'-cases.json')).read_text());probes=np.concatenate([np.arange(c['probe_start'],c['probe_end']) for c in cases])
    report=dict(version=version,step=checkpoint['step'],incremental_probe_error=metrics(target[probes],predicted[probes]),reference=measure(target,name),model=measure(predicted,name))
    (ROOT/f'{name}-{version}.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['capture','analyze','candidate']);parser.add_argument('--version',choices=['v15','v16','v17','v18'],default='v15');parser.add_argument('--validation',action='store_true');args=parser.parse_args()
    if args.mode=='capture':capture(args.validation)
    elif args.mode=='analyze':analyze()
    else:candidate(args.version,'crest-validation' if args.validation else 'crest-history')
