"""v24: spectral controller refinement with independently measured recovery.

Dense release-envelope captures become training data. Newly captured release-
generalization cases are never loaded here. v23's fixed selection is the warm
start; ongoing waveform/crest selection weights remain unchanged.
"""
import argparse,json
import torch
import p821_spectral_control_model as previous
from p821_release_objective import ReleaseObjective
from p821_identification_analysis import ROOT

Model=previous.Model
prepare_source=previous.prepare_source
core=previous.core


class Auxiliary:
    def __init__(self):
        self.crest=previous.Auxiliary();self.release=ReleaseObjective(prepare_source,weight=.1)

    def __call__(self,model,step):
        # Both objectives traverse every case; using the unscaled outer step
        # for crest would inadvertently skip alternate conditioner pairs.
        return self.crest(model,step//2) if step%8==0 else self.release(model,step)


def initialize(m):
    checkpoint=torch.load(ROOT/'surrogate-v23-continuous-selected.pt',weights_only=True);m.load_state_dict(checkpoint['state_dict']);print('Warm start selected v23',checkpoint['step'],'with dense recovery supervision',flush=True)


def activate():
    previous.activate();core.VERSION='v24'


def main(steps):
    assert (ROOT/'selection-v23-continuous.json').exists()
    plan=json.loads((ROOT/'selection-plan-v23.json').read_text());plan.update(candidate_family='v24',candidate_steps=f'All completed 500-step checkpoints, 0 through {steps}',expected_final_step=steps,new_training='release-envelope-plus/minus, paired post-burst three-pilot waveform objective, alternates with crest objective',extra_unfit_diagnostic='release-generalization-plus/minus');(ROOT/'selection-plan-v24.json').write_text(json.dumps(plan,indent=2))
    activate();core.train(steps,learning_rate=.00008,decay=True,initializer=initialize,retain_checkpoints=True,auxiliary_factory=Auxiliary)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--steps',type=int,default=16000);main(parser.parse_args().steps)
