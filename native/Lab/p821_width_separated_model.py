"""v20: separate measured dynamic stereo width from the channel tape model.

Matched Focus captures supply channel-model training targets. Inference uses
only the source audio and a frozen, separately trained width controller. The
reference Focus-pair trajectory is never used by prepare_source or prediction.
"""
import argparse,json,hashlib
import numpy as np
import torch
import p821_separated_model as core
from p821_history_gate_model import Model
from p821_peak_control_model import prepare_source as previous_prepare
from p821_width_model import Model as WidthModel,features
from p821_width_identify import FOCUS_FULL_DB,BASE_DIFFERENCE_DB
from p821_identification_analysis import ROOT,read
from p821_incremental_objective import IncrementalObjective

WIDTH_CHECKPOINT='width-surrogate-v20-frozen.pt'
_width_model=None


def sample_width(delta,n):
    fraction=np.arange(1,257)[None,:]/256
    db=(delta[:-1,None]*(1-fraction)+delta[1:,None]*fraction).reshape(-1)
    gain=10**(db*FOCUS_FULL_DB/BASE_DIFFERENCE_DB/20)
    return np.pad(gain,(0,max(0,n-len(gain))),constant_values=1)[:n]


@torch.no_grad()
def prepare_source(source):
    global _width_model
    if _width_model is None:
        checkpoint=torch.load(ROOT/WIDTH_CHECKPOINT,weights_only=True)
        _width_model=WidthModel();_width_model.load_state_dict(checkpoint['state_dict']);_width_model.eval()
    d=previous_prepare(source);delta=_width_model(features(d['x'].astype(np.float64))[None])[0].numpy()
    d['extra_width_gain']=sample_width(delta,len(d['x']))
    return d


def strip_reference_width(name,y):
    path=ROOT/f'width-labels-{name}.npz'
    if not path.exists():
        # These training inputs are exactly mono or have an exactly silent side.
        assert name in ['long-levels','structure-left','structure-mono'],name
        return y
    delta=np.load(path)['focus_delta_db'];gain=sample_width(delta,len(y))
    mid=y.mean(axis=1);side=(y[:,0]-y[:,1])*.5/gain
    return np.column_stack([mid+side,mid-side])


def target_transform(name,y,d):
    if name in ['music-levels','history-plus','quality-tones','detector-pulses']:
        return d['target']  # Validation scoring always uses the untouched output.
    target=strip_reference_width(name,y)@np.linalg.inv(d['w']).T
    return torch.from_numpy(target.astype(np.float32))


class SeparatedIncrementalObjective(IncrementalObjective):
    def __init__(self):
        super().__init__(shape_weight=100.)
        plus=strip_reference_width('crest-history-plus',read('crest-history-plus'))
        minus=strip_reference_width('crest-history-minus',read('crest-history-minus'))
        target=(plus-minus)*.5@np.linalg.inv(self.data[0]['w']).T
        self.targets=[torch.from_numpy(target[c['probe_start']+24:c['probe_end']-24].T.copy()) for c in self.cases]


def initialize(m):
    checkpoint=torch.load(ROOT/'surrogate-v19-selected.pt',weights_only=True)
    m.load_state_dict(checkpoint['state_dict']);print('Warm start v19',checkpoint['step'],'with separate frozen width model',flush=True)


def activate():
    core.VERSION='v20';core.EXTRA_TRAINING=['saturation-map','training-recovery'];core.prepare_source=prepare_source;core.Model=Model
    core.LENGTH=32768;core.CONTEXT=24576;core.TARGET_TRANSFORM=target_transform


def main(steps):
    source=ROOT/'width-surrogate-v2-best.pt';destination=ROOT/WIDTH_CHECKPOINT
    if not destination.exists():destination.write_bytes(source.read_bytes())
    plan=json.loads((ROOT/'selection-plan-v19.json').read_text());plan['candidate_family']='v20';plan['candidate_steps']=f'All completed 500-step checkpoints, 0 through {steps}';plan['width_checkpoint']=WIDTH_CHECKPOINT;plan['width_checkpoint_sha256']=hashlib.sha256(destination.read_bytes()).hexdigest();plan['continuous_state']=True
    (ROOT/'selection-plan-v20.json').write_text(json.dumps(plan,indent=2))
    activate();core.train(steps,learning_rate=.0002,decay=True,initializer=initialize,retain_checkpoints=True,auxiliary_factory=SeparatedIncrementalObjective)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--steps',type=int,default=10000);main(parser.parse_args().steps)
