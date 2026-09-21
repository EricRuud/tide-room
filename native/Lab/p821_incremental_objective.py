"""Explicitly fit small changes in response after equal-RMS conditioners.

The crest-history captures become TRAINING data when this objective is enabled.
They must no longer be described as independent validation for this model.
"""
import json
import numpy as np
import torch
from p821_identification_analysis import ROOT,read
from p821_peak_control_model import prepare_source


class IncrementalObjective:
    def __init__(self,shape_weight=2.):
        self.shape_weight=shape_weight
        self.data=[]
        for name in ['crest-history-plus','crest-history-minus']:
            job=json.loads((ROOT/(name+'.json')).read_text())['job'];self.data.append(prepare_source(job['input']))
        w=self.data[0]['w'];target=(read('crest-history-plus')-read('crest-history-minus'))*.5@np.linalg.inv(w).T
        self.cases=json.loads((ROOT/'crest-history-cases.json').read_text());self.targets=[]
        for c in self.cases:self.targets.append(torch.from_numpy(target[c['probe_start']+24:c['probe_end']-24].T.copy()))
        print('Incremental objective: crest-history is now training data',flush=True)

    def __call__(self,model,step):
        pair=(step//4)%(len(self.cases)//2);channel=(step//(2*len(self.cases)))%2
        indices=[pair*2,pair*2+1];x=[];controls=[]
        for i in indices:
            c=self.cases[i];a=c['start'];b=a+192*256
            for d in self.data:
                x.append(d['base'][a:b,channel]);controls.append(d['control'][a//256:b//256+1,channel])
        output=model(torch.stack(x),torch.stack(controls))
        c=self.cases[indices[0]];a=c['probe_start']-c['start']+24;b=c['probe_end']-c['start']-24
        pred=torch.stack([(output[0,a:b]-output[1,a:b])*.5,(output[2,a:b]-output[3,a:b])*.5])
        target=torch.stack([self.targets[i][channel] for i in indices]);energy=torch.mean(target**2).clamp_min(1e-12)
        absolute=torch.mean((pred-target)**2)/energy
        shape=torch.mean(((pred[1]-pred[0])-(target[1]-target[0]))**2)/energy
        return .25*absolute+self.shape_weight*shape
