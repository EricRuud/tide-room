"""Small polarity-differenced impulses after left-only limiting conditioners.

The first impulse response sample measures combined instantaneous gain and
filter feedthrough. It is not the isolated limiter gain without further analysis.
These captures are diagnostics, excluded from training.
"""
import argparse,json
import numpy as np
from p821_identify import ROOT,SR,save,submit
from p821_identification_analysis import read,linear_kernel,db


def capture(late=False):
    name='limiter-recovery-late' if late else 'limiter-recovery'
    cases=[];conditioners=[np.zeros((384*256,2))];probes=[np.zeros((384*256,2))];offset=len(conditioners[0])
    for level in [.7,1.4]:
        for gap in ([64,96,128,192,256,384] if late else [0,.125,.25,.5,.75,1,1.5,2,3,4,6,8,12,16,24,48]):
            c=np.zeros(((288 if late else 240)*256,2));p=np.zeros_like(c);a=48*256;b=a+96*256
            c[a:b,0]=level*np.sin(2*np.pi*187.5*np.arange(b-a)/SR)
            pa=b+round(gap*SR/1000);p[pa,0]=.006
            cases.append(dict(start=offset,conditioner_start=offset+a,conditioner_end=offset+b,probe_start=offset+pa,level=level,gap_ms=gap,probe_amplitude=.006))
            conditioners.append(c);probes.append(p);offset+=len(c)
    c=np.concatenate(conditioners);p=np.concatenate(probes)
    (ROOT/(name+'-cases.json')).write_text(json.dumps(cases,indent=2))
    for sign,suffix in [(1,'plus'),(-1,'minus')]:submit(name+'-'+suffix,save(name+'-'+suffix,c+sign*p))


def analyze(late=False):
    name='limiter-recovery-late' if late else 'limiter-recovery'
    h=linear_kernel();w=h[:,:,0]/(h[0,0,0]+h[0,1,0]);y=(read(name+'-plus')-read(name+'-minus'))*.5@np.linalg.inv(w).T;quiet=h[0,0,0]+h[0,1,0];rows=[]
    for c in json.loads((ROOT/(name+'-cases.json')).read_text()):
        a=c['probe_start'];ir=y[a:a+256,0]/c['probe_amplitude'];gain=ir[0]/quiet
        row=c|dict(direct_gain_db=db(abs(gain)),direct_sign=float(np.sign(gain)),first_32_samples=ir[:32].tolist());rows.append(row)
        print(c['level'],c['gap_ms'],round(row['direct_gain_db'],6),flush=True)
    (ROOT/(name+'-analysis.json')).write_text(json.dumps(rows,indent=2))


def candidate():
    import torch
    import p821_memory_control_model as extension
    extension.activate();core=extension.core;torch.set_num_threads(2)
    checkpoint=torch.load(ROOT/'surrogate-v18-best.pt',weights_only=True);m=core.Model();m.load_state_dict(checkpoint['state_dict']);m.eval();outputs=[]
    for suffix in ['plus','minus']:
        d=core.prepare_source(json.loads((ROOT/f'limiter-recovery-{suffix}.json').read_text())['job']['input']);outputs.append(core.predict(m,d))
    y=(outputs[0]-outputs[1])*.5@np.linalg.inv(d['w']).T;h=linear_kernel();quiet=h[0,0,0]+h[0,1,0];rows=[]
    reference=json.loads((ROOT/'limiter-recovery-analysis.json').read_text())
    for c in reference:
        a=c['probe_start'];ir=y[a:a+256,0]/c['probe_amplitude'];gain=ir[0]/quiet
        row=dict(level=c['level'],gap_ms=c['gap_ms'],reference_gain_db=c['direct_gain_db'],model_gain_db=db(abs(gain)));rows.append(row)
        print(row,flush=True)
    (ROOT/'limiter-recovery-v18.json').write_text(json.dumps(dict(step=checkpoint['step'],rows=rows),indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['capture','analyze','candidate']);parser.add_argument('--late',action='store_true');args=parser.parse_args()
    candidate() if args.mode=='candidate' else globals()[args.mode](args.late)
