"""v22: refine the selected v20 using broader RMS-controlled musical training."""
import argparse,json
import torch
import p821_width_separated_model as previous
from p821_identification_analysis import ROOT

Model=previous.Model
prepare_source=previous.prepare_source
core=previous.core


def initialize(m):
    checkpoint=torch.load(ROOT/'surrogate-v20-continuous-selected.pt',weights_only=True);m.load_state_dict(checkpoint['state_dict']);print('Warm start selected v20',checkpoint['step'],'new RMS-controlled musical training',flush=True)


def activate():
    previous.activate();core.VERSION='v22';core.EXTRA_TRAINING=['saturation-map','training-recovery','training-rich'];core.TRAINING_REPEATS={'training-rich':3}


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--steps',type=int,default=16000);args=parser.parse_args()
    plan=json.loads((ROOT/'selection-plan-v20.json').read_text());plan['candidate_family']='v22';plan['candidate_steps']=f'All completed 500-step checkpoints, 0 through {args.steps}';plan['expected_final_step']=args.steps;plan['new_training']='training-rich, generated independently of validation and final holdouts; three sampling shares';(ROOT/'selection-plan-v22.json').write_text(json.dumps(plan,indent=2))
    activate();core.train(args.steps,learning_rate=.0001,decay=True,initializer=initialize,retain_checkpoints=True,auxiliary_factory=previous.SeparatedIncrementalObjective)
