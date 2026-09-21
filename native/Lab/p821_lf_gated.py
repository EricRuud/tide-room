"""Restrict the LF shape change to bass-dominated input, with smooth transitions.

Convex coefficient interpolation keeps each stationary second-order denominator
inside its stable region; that is not a universal time-varying stability proof.
"""
import argparse,json,time
import numpy as np
import torch
from torchaudio.functional import lfilter
from p821_lf_training import Model as DynamicModel
from p821_lf_basis import Model as BasisModel
from p821_lf_experiments import ROOT,CHECKPOINT,SR,save_json
import p821_rest_state_model as parent


class Model(DynamicModel):
    threshold=.55
    amount=1.
    def dominance(self,features):
        c=features[:,:,:61].double();level=torch.pow(10.,(c[:,:,0]*30-30)/20)
        ratio=torch.pow(10.,(c[:,:,1]-c[:,:,0])*30/20)
        raw=((ratio-self.threshold)/.2).clamp(0,1)*((level-.08)/.04).clamp(0,1)
        alpha=np.exp(-256/(SR*.03));a=torch.tensor([1.,-alpha],dtype=torch.float64);b=torch.tensor([1-alpha,0.],dtype=torch.float64)
        return lfilter(raw,a,b,clamp=False)*self.amount
    def apply_correction(self,values,features):
        g=self.dominance(features);extra=self.correction(features)*g[:,:,None]
        return torch.cat([values[:,:,:2]+extra[:,:,:2],values[:,:,2:5],values[:,:,5:7]+extra[:,:,2:],values[:,:,7:],g[:,:,None]],dim=2)
    def filter_coefficients(self,values):
        old=parent.Model.filter_coefficients(self,values);new=BasisModel.filter_coefficients(self,values);g=values[:,:,9,None]
        new[0]=old[0]*(1-g)+new[0]*g;return new


def initialize(step=None,threshold=.55,amount=1.):
    m=Model();m.threshold=threshold;m.amount=amount;state=m.state_dict();state.update(torch.load(CHECKPOINT,weights_only=True)['state_dict'])
    if step is not None:state.update(torch.load(ROOT/f'dynamic-lf-step-{step}.pt',weights_only=True)['state_dict'])
    m.load_state_dict(state);m.eval();m.configure(next(c['parameters'] for c in json.loads((ROOT/'lf-basis-fit.json').read_text())['candidates'] if c['name']=='retuned-bell'));return m


def survey():
    from p821_identification_analysis import metrics
    from p821_lf_select import guard
    import soundfile as sf
    torch.set_num_threads(1)
    candidates=[dict(step=step,threshold=threshold,amount=amount) for step in [None,200,800] for threshold in [.55,.75] for amount in [.5,1.]]
    save_json(ROOT/'gated-lf-survey-plan.json',dict(candidates=candidates,fit=False,final_used=False))
    models=[initialize(**c) for c in candidates];rows=[dict(parameters=c,validation={}) for c in candidates]
    for name in ['validation-rounded','validation-sustain','validation-mixed']:
        d=parent.prepare_source(ROOT/f'input-{name}.wav');target,_=sf.read(ROOT/(name+'.wav'),always_2d=True)
        for m,row in zip(models,rows):
            y=parent.core.predict(m,d);assert np.isfinite(y).all();row['validation'][name]=metrics(target[SR:],y[SR:])['residual_dbr']
        del d,y,target;time.sleep(.2)
    baseline=np.mean([r['results']['baseline']['comparison']['residual_dbr'] for r in json.loads((ROOT/'lf-basis-validation.json').read_text())['rows']])
    for row in rows:row['mean']=float(np.mean(list(row['validation'].values())))
    save_json(ROOT/'gated-lf-survey.json',dict(rows=rows,complete=False,baseline=float(baseline)));chosen=None
    for row in sorted(rows,key=lambda r:r['mean']):
        print(row,flush=True)
        if row['mean']>baseline-.2:continue
        m=initialize(**row['parameters']);row['guard']=guard(m);print('Guard',row['guard']['weighted'],row['guard']['passes'],flush=True)
        save_json(ROOT/'gated-lf-survey.json',dict(rows=rows,complete=False,baseline=float(baseline)))
        if row['guard']['passes']:chosen=row;break
    save_json(ROOT/'gated-lf-survey.json',dict(rows=rows,complete=True,baseline=float(baseline),chosen=chosen,final_used=False));print('Chosen',chosen,flush=True)


if __name__=='__main__':survey()
