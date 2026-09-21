"""Selection for the second LF study; original final recordings stay excluded."""
import argparse,json,time
import numpy as np
import torch
import soundfile as sf
from p821_lf_training import Model,ROOT,OLD,SR,parent,initialize
from p821_lf_experiments import CHECKPOINT,digest,save_json
from p821_identification_analysis import read,metrics
from p821_select_staged import paired_shape


def load(step):
    m=initialize();saved=torch.load(ROOT/f'dynamic-lf-step-{step}.pt',weights_only=True);m.load_state_dict(saved['state_dict']);m.configure(saved['basis']);m.eval();return m


def guard(model):
    scores={};hygiene={};paired=[]
    for name in ['music-levels','history-plus','quality-tones','crest-validation-plus','crest-validation-minus','quality-silence','quality-dc']:
        source=json.loads((OLD/(name+'.json')).read_text())['job']['input'];d=parent.prepare_source(source);prediction=parent.core.predict(model,d);target=read(name)
        assert np.isfinite(prediction).all();value=metrics(target[SR:],prediction[SR:])
        if name.startswith('crest-'):paired.append(prediction)
        elif name.startswith('quality-') and name!='quality-tones':hygiene[name]=value|dict(peak=float(np.max(abs(prediction))),exact_zero=not np.count_nonzero(prediction))
        else:scores[name]=value['residual_dbr']
        del d,target,prediction;time.sleep(.2)
    cases=json.loads((OLD/'crest-validation-cases.json').read_text());target_probe=(read('crest-validation-plus')-read('crest-validation-minus'))*.5
    scores['crest-validation-shape-change']=metrics(paired_shape(target_probe,cases),paired_shape((paired[0]-paired[1])*.5,cases))['residual_dbr']
    weights=json.loads((ROOT/'candidate-protocol.json').read_text()).get('old_weights',{'music-levels':.45,'history-plus':.2,'quality-tones':.1,'crest-validation-shape-change':.25})
    weighted=sum(weights[k]*v for k,v in scores.items());baseline=json.loads((OLD/'selection-v31rest-continuous.json').read_text())['selected']['score']
    return dict(components=scores,weighted=weighted,baseline_weighted=baseline,passes=weighted<=baseline+.15 and hygiene['quality-silence']['exact_zero'],hygiene=hygiene)


def select(prefix='dynamic-lf',loader=load):
    torch.set_num_threads(1);plan=json.loads((ROOT/f'{prefix}-plan.json').read_text());steps=list(range(0,plan['steps']+1,200));assert (ROOT/f'{prefix}-step-{steps[-1]}.pt').exists(),'Wait for completed training'
    rows=[]
    for step in steps:
        m=loader(step);values={}
        for name in ['validation-rounded','validation-sustain','validation-mixed']:
            d=parent.prepare_source(ROOT/f'input-{name}.wav');y=parent.core.predict(m,d);target,_=sf.read(ROOT/(name+'.wav'),always_2d=True)
            assert np.isfinite(y).all();values[name]=metrics(target[SR:],y[SR:])['residual_dbr'];del d,y,target
        row=dict(step=step,validation=values,mean=float(np.mean(list(values.values()))));rows.append(row)
        save_json(ROOT/f'{prefix}-selection-progress.json',dict(rows=rows,complete=False));print('Step',step,values,'mean',row['mean'],flush=True);time.sleep(.2)
    baseline_rows=json.loads((ROOT/'lf-basis-validation.json').read_text())['rows'];baseline=float(np.mean([r['results']['baseline']['comparison']['residual_dbr'] for r in baseline_rows]));chosen=None
    for row in sorted(rows,key=lambda r:r['mean']):
        if row['mean']>baseline-.2:continue
        m=loader(row['step']);row['guard']=guard(m);print('Guard',row['step'],row['guard']['weighted'],row['guard']['passes'],flush=True)
        save_json(ROOT/f'{prefix}-selection-progress.json',dict(rows=rows,complete=False))
        if row['guard']['passes']:chosen=row;break
    save_json(ROOT/f'{prefix}-selection.json',dict(rows=rows,baseline_new_validation_mean=baseline,chosen=chosen,complete=True,final_used=False,
      parent_sha256=digest(CHECKPOINT),selection_rule=json.loads((ROOT/'candidate-protocol.json').read_text())['selection']))
    print('Chosen',chosen,flush=True)


if __name__=='__main__':select()
