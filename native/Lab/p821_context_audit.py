"""Compare whole-sequence state evolution with contextual chunk rendering.

This measures implementation consistency, not accuracy against P821. A weak
residual would require longer context or explicit full state carry before a
claimed deep musical null. Final musical holdouts are excluded.
"""
import argparse,json,time
import numpy as np
import torch
from p821_identification_analysis import ROOT,metrics


@torch.no_grad()
def main(version):
    if version=='v19':import p821_history_gate_model as extension
    else:import p821_memory_control_model as extension
    extension.activate();core=extension.core;torch.set_num_threads(2);checkpoint=torch.load(ROOT/f'surrogate-{version}-selected.pt',weights_only=True);m=core.Model();m.load_state_dict(checkpoint['state_dict']);m.eval();rows={}
    for name in ['music-levels','limiter-recovery-plus']:
        d=core.prepare_source(json.loads((ROOT/(name+'.json')).read_text())['job']['input'])
        # Bound this diagnostic to the first eight seconds, with original history.
        length=min(len(d['x']),48000*8)//256*256
        d={**d,'x':d['x'][:length],'base':d['base'][:length],'control':d['control'][:length//256+1],'length':length}
        whole=(m(d['base'].T,d['control'].transpose(0,1)).T.numpy()@d['w'].T)[:length];chunked=core.predict_contextual(m,d)
        rows[name]=metrics(whole,chunked);print(name,rows[name],flush=True)
    (ROOT/f'context-audit-{version}.json').write_text(json.dumps(dict(version=version,step=checkpoint['step'],whole_vs_chunks=rows),indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('version',choices=['v18','v19']);main(parser.parse_args().version)
