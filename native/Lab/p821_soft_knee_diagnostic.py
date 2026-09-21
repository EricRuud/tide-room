"""Input-only soft-knee perturbation of a selected model, with no refitting.

The standard quadratic dB knee is an alternative hypothesis for the final
limiter. It is not asserted to be P821's curve or a tape-physics equation.
"""
import argparse,importlib,json
import numpy as np
import torch
from p821_identification_analysis import ROOT,SR,read,metrics
from p821_select_continuous import MODULES
from p821_stateful_render import parts,mix
from p821_memory_limiter import forward
from p821_select_staged import paired_shape


def apply(x,envelope,knee):
    if knee==0:return x/np.maximum(1,envelope)
    level=20*np.log10(np.maximum(envelope,1e-15));reduction=np.zeros_like(level)
    high=level>=knee/2;transition=(level>-knee/2)&~high
    reduction[high]=level[high];reduction[transition]=(level[transition]+knee/2)**2/(2*knee)
    return x*np.power(10.,-reduction/20)


def main(version):
    torch.set_num_threads(1);extension=importlib.import_module(MODULES[version]);extension.activate()
    saved=torch.load(ROOT/f'surrogate-{version}-continuous-selected.pt',weights_only=True);model=extension.Model();model.load_state_dict(saved['state_dict']);model.eval()
    knees=[0.,.25,.5,1.,2.,4.,8.];rows={str(k):dict(knee_db=k,components={}) for k in knees};shapes={};target_shapes={}
    cases=json.loads((ROOT/'crest-validation-cases.json').read_text())
    for name in ['music-levels','history-plus','quality-tones','crest-validation-plus','crest-validation-minus']:
        d=extension.core.prepare_source(json.loads((ROOT/(name+'.json')).read_text())['job']['input']);target=read(name)
        pre,cap=parts(model,d);native,envelope,_=forward(pre.T.copy(),cap.T.copy(),np.exp(-1/(SR*float(model.release.exp().detach()))),np.zeros(2));envelope=envelope.T
        if name.startswith('crest-validation'):shapes[name]={};target_shapes[name]=paired_shape(target,cases)
        for knee in knees:
            out=mix(apply(pre,envelope,knee),d)
            if knee==0:assert np.max(abs(out-mix(native.T,d)))<1e-12
            if name.startswith('crest-validation'):shapes[name][str(knee)]=paired_shape(out,cases)
            else:
                row=rows[str(knee)];row['components'][name]=metrics(target[SR:],out[SR:])['residual_dbr']
                if name=='music-levels':row['music_passages']=[c|metrics(target[c['start']:c['end']],out[c['start']:c['end']]) for c in json.loads((ROOT/'input-music-levels.json').read_text())]
        print(name,{k:round(r['components'].get(name,0),3) for k,r in rows.items()},flush=True)
        del d,target,pre,cap,native,envelope,out
    target=(target_shapes['crest-validation-plus']-target_shapes['crest-validation-minus'])*.5
    weights=json.loads((ROOT/f'selection-{version}-continuous.json').read_text())['plan']['weights']
    for key,row in rows.items():
        predicted=(shapes['crest-validation-plus'][key]-shapes['crest-validation-minus'][key])*.5
        row['components']['crest-validation-shape-change']=metrics(target,predicted)['residual_dbr']
        row['score']=sum(weights[name]*value for name,value in row['components'].items())
    result=dict(version=version,step=saved['step'],no_weights_refitted=True,final_holdouts_used=False,weights=weights,rows=list(rows.values()))
    (ROOT/f'soft-knee-diagnostic-{version}.json').write_text(json.dumps(result,indent=2))
    print('Overall',[(r['knee_db'],round(r['score'],6)) for r in rows.values()],flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('version',choices=list(MODULES));main(parser.parse_args().version)
