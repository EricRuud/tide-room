"""Train post-conditioner response using matched quiet-pilot perturbations.

Enabling this objective makes release-envelope-plus/minus TRAINING data. The
paired difference removes the loud conditioner from the target. The loss uses
the entire three-pilot waveform after release, so it does not assume that the
remaining response is a pure gain envelope or discard its phase information.
"""
import json
import numpy as np
import torch
from p821_identification_analysis import ROOT,read


class ReleaseObjective:
    def __init__(self,prepare_source,weight=.1):
        self.weight=weight;self.data=[]
        for label in ['plus','minus']:
            name='release-envelope-'+label;source=json.loads((ROOT/(name+'.json')).read_text())['job']['input'];self.data.append(prepare_source(source))
        target=(read('release-envelope-plus')-read('release-envelope-minus'))*.5@np.linalg.inv(self.data[0]['w']).T
        self.cases=json.loads((ROOT/'release-envelope-cases.json').read_text());self.targets=[];self.energy=[]
        for c in self.cases:
            a=c['conditioner_end']+24;b=c['conditioner_end']+96*256
            self.targets.append(torch.from_numpy(target[a:b,0].copy()))
            before=target[c['conditioner_start']-16*256:c['conditioner_start']-4*256,0]
            self.energy.append(float(np.mean(before**2)))
        print('Dense release-envelope captures are now training data; three-pilot waveform loss',flush=True)

    def __call__(self,model,step):
        i=(step//8)%len(self.cases);c=self.cases[i]
        a=c['start'];b=c['conditioner_end']+96*256
        x=torch.stack([d['base'][a:b,0] for d in self.data]);controls=torch.stack([d['control'][a//256:b//256+1,0] for d in self.data])
        output=model(x,controls);begin=c['conditioner_end']-a+24;pred=(output[0,begin:]-output[1,begin:])*.5
        # Normalize by the quiet pre-conditioner pilots, avoiding division by a
        # strongly attenuated post-burst signal and unequal weighting by level.
        loss=torch.mean((pred-self.targets[i])**2)/max(self.energy[i],1e-16)
        return self.weight*loss


def check():
    from p821_spectral_control_model import Model,initialize,prepare_source
    torch.set_num_threads(2);model=Model();initialize(model);objective=ReleaseObjective(prepare_source)
    for step in [0,8*7,8*18,8*43,8*59]:
        model.zero_grad();loss=objective(model,step);loss.backward()
        assert torch.isfinite(loss) and all(p.grad is not None and torch.isfinite(p.grad).all() for p in model.parameters())
        print('case',step//8,'loss',float(loss.detach()),'finite gradients',flush=True)


if __name__=='__main__':check()
