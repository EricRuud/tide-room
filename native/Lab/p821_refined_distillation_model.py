"""v27: waveform refinement of v26's strongest development-music candidate.

Direct trajectory fitting improves the music validation but forgets other
responses. Restore broader waveform supervision while retaining a small latent
objective on synthetic training cases. All fitting remains target-free at
inference. Fixed filter shapes keep the latent labels in the same basis.
"""
import argparse,hashlib,json
import numpy as np
import torch
import p821_distilled_control_model as previous
from p821_spectral_control_model import Auxiliary as CrestObjective
from p821_identification_analysis import ROOT

Model=previous.Model
prepare_source=previous.prepare_source
core=previous.core


def activate():
    previous.activate();core.VERSION='v27'


class Auxiliary:
    def __init__(self):
        self.crest=CrestObjective();self.labels,_=previous.load_labels();self.rng=np.random.default_rng(27821)

    def __call__(self,model,step):
        if step%8==0:return self.crest(model,step//2)
        return .002*previous.latent_batch_loss(model,self.labels,self.rng,step//8)


def initialize(model):
    checkpoint=torch.load(ROOT/'surrogate-v26-music-selected.pt',weights_only=True)
    model.load_state_dict(checkpoint['state_dict']);model.frequency.requires_grad_(False);model.q.requires_grad_(False)
    print('Warm start v26 music candidate step',checkpoint['step'],'with frozen filter basis',flush=True)


def main(steps):
    activate();training=json.loads((ROOT/'surrogate-v26-training.json').read_text())
    source_plan=json.loads((ROOT/'selection-plan-v26.json').read_text());assert training[-1]['step']==source_plan['expected_final_step']
    best=min(training,key=lambda row:row['validation']['music-levels']['residual_dbr'])
    source=ROOT/f"surrogate-v26-step-{best['step']}.pt";destination=ROOT/'surrogate-v26-music-selected.pt'
    destination.write_bytes(source.read_bytes())
    plan=json.loads((ROOT/'selection-plan-v23.json').read_text())
    plan.update(candidate_family='v27',expected_final_step=steps,candidate_steps=f'All completed 500-step checkpoints, 0 through {steps}',
                initialization=dict(version='v26',step=best['step'],criterion='minimum development music residual; initialization only, not the overall selection criterion',
                                    sha256=hashlib.sha256(destination.read_bytes()).hexdigest()),
                structural_change='v26 controller; fixed v23 filter shapes; broader waveform refinement and small synthetic latent objective',
                auxiliary='Original spectral crest objective alternates with 0.002 latent objective every four steps; no dense release objective',
                final_holdouts_used=False)
    (ROOT/'selection-plan-v27.json').write_text(json.dumps(plan,indent=2))
    core.train(steps,learning_rate=.00003,decay=True,initializer=initialize,retain_checkpoints=True,auxiliary_factory=Auxiliary)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--steps',type=int,default=16000);main(parser.parse_args().steps)
