"""Apply the written v17 selection plan; final musical holdouts remain unopened."""
import argparse,json
import numpy as np
import torch
from p821_identification_analysis import ROOT,read,metrics
import p821_staged_clip_model as extension


def paired_shape(y,cases):
    result=[]
    for lo,hi in zip(cases[::2],cases[1::2]):
        low=y[lo['probe_start']+24:lo['probe_end']-24]
        high=y[hi['probe_start']+24:hi['probe_end']-24]
        result.append(high-low)
    return np.concatenate(result)


def main(version):
    if version=='v18':
        import p821_memory_control_model as extension
    elif version=='v19':
        import p821_history_gate_model as extension
    elif version=='v20':
        import p821_width_separated_model as extension
    elif version=='v21':
        import p821_detector_bank_model as extension
    else:
        import p821_staged_clip_model as extension
    extension.activate();core=extension.core;torch.set_num_threads(2)
    plan=json.loads((ROOT/f'selection-plan-{version}.json').read_text());training=json.loads((ROOT/f'surrogate-{version}-training.json').read_text())
    data=[]
    for suffix in ['plus','minus']:
        job=json.loads((ROOT/f'crest-validation-{suffix}.json').read_text())['job'];data.append(core.prepare_source(job['input']))
    target=(read('crest-validation-plus')-read('crest-validation-minus'))*.5;cases=json.loads((ROOT/'crest-validation-cases.json').read_text());target_shape=paired_shape(target,cases)
    probe_indices=np.concatenate([np.arange(c['probe_start']+24,c['probe_end']-24) for c in cases]);rows=[];best=None
    for logged in training:
        step=logged['step'];checkpoint=torch.load(ROOT/f'surrogate-{version}-step-{step}.pt',weights_only=True);m=core.Model();m.load_state_dict(checkpoint['state_dict']);m.eval()
        outputs=[core.predict(m,d) for d in data];predicted=(outputs[0]-outputs[1])*.5
        shape=metrics(target_shape,paired_shape(predicted,cases));probe=metrics(target[probe_indices],predicted[probe_indices])
        values={k:logged['validation'][k]['residual_dbr'] for k in ['music-levels','history-plus','quality-tones']};values['crest-validation-shape-change']=shape['residual_dbr']
        score=sum(plan['weights'][k]*v for k,v in values.items())
        row=dict(step=step,score=score,components=values,crest_probe=probe,crest_shape=shape)
        if hasattr(m,'early_amount'):row.update(early_mix=float(1.5*torch.tanh(m.early_amount).detach()),early_ratio=float(torch.exp(m.early_ratio).detach()))
        if hasattr(m,'release'):row['release_seconds']=float(torch.exp(m.release).detach())
        rows.append(row);print(step,'score',round(score,2),'shape',round(shape['residual_dbr'],2),'music',round(values['music-levels'],2),flush=True)
        if best is None or score<best['score']:best=row;torch.save(checkpoint,ROOT/f'surrogate-{version}-selected.pt')
        (ROOT/f'selection-{version}.json').write_text(json.dumps(dict(plan=plan,selected=best,candidates=rows,final_holdouts_used=False),indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--version',choices=['v17','v18','v19','v20','v21'],default='v17');main(parser.parse_args().version)
