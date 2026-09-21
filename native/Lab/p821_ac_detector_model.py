"""v28: reject constant offsets in the fast controller's measured input.

DC pilot probes motivate a 5 Hz second-order high-pass before fast descriptors.
The quiet audio path, gentle gain and stereo width still receive the original
input. This is an input-only routing hypothesis, not a recovered vendor circuit.
"""
import argparse,hashlib,json
import torch
import p821_crest_gate_model as previous
from p821_spectral_control_model import Auxiliary as CrestObjective
from p821_detector_placement import change
from p821_identification_analysis import ROOT

Model=previous.Model
core=previous.core


def prepare_source(source):return change(previous.prepare_source(source),5.,2)


class Auxiliary(CrestObjective):
    def __init__(self):
        super().__init__();self.data=[change(d,5.,2) for d in self.data]


def activate():
    previous.activate();core.VERSION='v28';core.prepare_source=prepare_source


def initialize(model):
    checkpoint=torch.load(ROOT/'surrogate-v25-continuous-selected.pt',weights_only=True)
    model.load_state_dict(checkpoint['state_dict']);print('Warm start v25',checkpoint['step'],'fast descriptors now AC coupled',flush=True)


def main(steps):
    activate();source=ROOT/'surrogate-v25-continuous-selected.pt'
    plan=json.loads((ROOT/'selection-plan-v23.json').read_text())
    plan.update(candidate_family='v28',expected_final_step=steps,candidate_steps=f'All completed 500-step checkpoints, 0 through {steps}',
                initialization=dict(version='v25',selection='continuous weighted criterion',sha256=hashlib.sha256(source.read_bytes()).hexdigest()),
                structural_change='5 Hz second-order high-pass before fast descriptors; original audio/slow/width inputs',
                training='Broader synthetic waveform data and original crest objective; no dense release objective or trajectory labels',
                final_holdouts_used=False)
    (ROOT/'selection-plan-v28.json').write_text(json.dumps(plan,indent=2))
    core.train(steps,learning_rate=.00003,decay=True,initializer=initialize,retain_checkpoints=True,auxiliary_factory=Auxiliary)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--steps',type=int,default=16000);main(parser.parse_args().steps)
