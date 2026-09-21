"""Does a past loud sound still alter a new note after long silence?

Fresh synthetic development diagnostic. Warmup is common silence, no playback.
Each gap is a separate render, with a corresponding unconditioned reference.
"""
import argparse,hashlib,importlib,json
import numpy as np
import torch
from p821_identify import ROOT,SR,save,submit
from p821_identification_analysis import read,metrics
from p821_select_continuous import MODULES


def capture():
    cases=[]
    for gap in [1,5,15,60]:
        a=384*256;b=a+24*256;probe_start=b+round(gap*SR/256)*256;probe_end=probe_start+75*256;n=probe_end+192*256
        x=np.zeros((n,2));t=np.arange(probe_end-probe_start)/SR;env=np.ones(len(t));env[:48]=np.linspace(0,1,48);env[-48:]=np.linspace(1,0,48)
        probe=env*(.45*np.sin(2*np.pi*375*t)+.15*np.sin(2*np.pi*9000*t))
        x[probe_start:probe_end]=probe[:,None]*[1,.6]
        case=dict(gap_seconds=(probe_start-b)/SR,requested_gap=gap,conditioner_start=a,conditioner_end=b,probe_start=probe_start,probe_end=probe_end,length=n)
        for conditioned in [False,True]:
            label=f'long-memory-{gap}-'+('conditioned' if conditioned else 'baseline');source=x.copy()
            if conditioned:
                t=np.arange(b-a)/SR;env=np.ones(len(t));env[:24]=np.linspace(0,1,24);env[-24:]=np.linspace(1,0,24)
                source[a:b]=(env*(.8*np.sin(2*np.pi*250*t)+.4*np.sin(2*np.pi*7000*t)))[:,None]*[1,.6]
            submit(label,save(label,source),warmupInput=False)
        cases.append(case);(ROOT/'long-memory-cases.json').write_text(json.dumps(cases,indent=2))
    analyze([])


def analyze(versions):
    torch.set_num_threads(1);cases=json.loads((ROOT/'long-memory-cases.json').read_text());rows=[]
    checkpoint_hashes={v:hashlib.sha256((ROOT/f'surrogate-{v}-continuous-selected.pt').read_bytes()).hexdigest() for v in versions}
    for case in cases:
        gap=case['requested_gap'];a=case['probe_start'];b=case['probe_end'];names=[f'long-memory-{gap}-{kind}' for kind in ['baseline','conditioned']]
        target=[read(name) for name in names];row=case|dict(reference_history_effect=metrics(target[0][a:b],target[1][a:b]),models={})
        for version in versions:
            extension=importlib.import_module(MODULES[version]);extension.activate();model=extension.Model();model.load_state_dict(torch.load(ROOT/f'surrogate-{version}-continuous-selected.pt',weights_only=True)['state_dict']);model.eval();output=[];without_width=[]
            for name in names:
                d=extension.prepare_source(ROOT/f'input-{name}.wav');output.append(extension.core.predict(model,d))
                y=output[-1][a:b];mid=y.mean(axis=1);side=(y[:,0]-y[:,1])*.5/d['extra_width_gain'][a:b]
                without_width.append(np.column_stack([mid+side,mid-side]));del d
            row['models'][version]=dict(history_effect=metrics(output[0][a:b],output[1][a:b]),
                without_dynamic_width_history_effect=metrics(without_width[0],without_width[1]),
                baseline_accuracy=metrics(target[0][a:b],output[0][a:b]),conditioned_accuracy=metrics(target[1][a:b],output[1][a:b]))
            del output
        rows.append(row);print(gap,'reference',row['reference_history_effect']['residual_dbr'],{v:r['history_effect']['residual_dbr'] for v,r in row['models'].items()},flush=True)
    suffix='-'+'-'.join(versions) if versions else '-reference'
    (ROOT/f'long-memory-audit{suffix}.json').write_text(json.dumps(dict(final_holdouts_used=False,used_for_fitting=False,checkpoint_hashes=checkpoint_hashes,rows=rows),indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['capture','audit']);parser.add_argument('--versions',nargs='*',default=['v27','v31']);args=parser.parse_args();capture() if args.mode=='capture' else analyze(args.versions)
