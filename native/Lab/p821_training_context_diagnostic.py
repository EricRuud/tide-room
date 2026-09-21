"""Measure finite training warm-up against full-state inference on development audio.

Compares the model to itself, not a new P821 null. No fitting or final audio.
"""
import argparse,importlib,json,time
import numpy as np
import torch
from p821_identification_analysis import ROOT,SR,metrics
from p821_select_continuous import MODULES
from p821_stateful_render import mix


@torch.no_grad()
def main(version):
    torch.set_num_threads(1);extension=importlib.import_module(MODULES[version]);extension.activate();model=extension.Model()
    saved=torch.load(ROOT/f'surrogate-{version}-continuous-selected.pt',weights_only=True);model.load_state_dict(saved['state_dict']);model.eval();rows=[]
    for name in ['music-levels','history-plus']:
        d=extension.prepare_source(json.loads((ROOT/(name+'.json')).read_text())['job']['input']);whole=extension.core.predict(model,d)
        n=len(d['base']);length=8192;positions=np.linspace(2.2*SR,n-length-256,24).astype(int)//256*256
        for context in [3072,6144,12288,24576,49152,98304]:
            originals=[];shortened=[];windows=[]
            for position in positions:
                a=position-context;b=position+length
                if a<0:continue
                x=d['base'][a:b].T;control=d['control'][a//256:b//256+1].transpose(0,1)
                short=model(x,control).T.numpy()[context:]
                subset=dict(w=d['w'],length=length,extra_width_gain=d['extra_width_gain'][position:b])
                short=mix(short,subset);reference=whole[position:b]
                windows.append(dict(start=int(position),comparison=metrics(reference,short)));originals.append(reference);shortened.append(short)
            result=metrics(np.concatenate(originals),np.concatenate(shortened))
            row=dict(dataset=name,context_samples=context,context_seconds=context/SR,comparison=result,windows=windows)
            rows.append(row);print(name,context/SR,result['residual_dbr'],flush=True)
        del d,whole
    (ROOT/f'training-context-{version}.json').write_text(json.dumps(dict(version=version,step=saved['step'],scope='Model self-consistency; not P821 accuracy',final_holdouts_used=False,rows=rows),indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('version',default='v27',nargs='?');main(parser.parse_args().version)
