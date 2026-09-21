"""v23: bandpass RMS/crest descriptors for the level-dependent filter controls.

The fitted-trajectory diagnostic shows excessive predicted mid cuts on some
bass-dominant material. Eight bandpass descriptors distinguish spectral drive
more directly than differences between broad low/high-pass energies. Training
and width handling match v22; no diagnostic music trajectories are reused.
"""
import argparse,json
import numpy as np
import torch
from torch import nn
from scipy import signal
from p821_identification_analysis import ROOT,SR
from p821_history_gate_model import Model as PreviousModel
from p821_width_separated_model import prepare_source as previous_prepare,SeparatedIncrementalObjective,target_transform
import p821_separated_model as core


def augment(d):
    x=d['x'].astype(np.float64);descriptors=[]
    for frequency in [45,150,500,1800,2700,6000,10000,16000]:
        b,a=signal.iirpeak(frequency,.7,fs=SR);y=signal.lfilter(b,a,x,axis=0).reshape(-1,256,2)
        for amplitude in [np.sqrt(np.mean(y*y,axis=1)),np.max(abs(y),axis=1)/np.sqrt(2)]:descriptors.append(np.clip((20*np.log10(np.maximum(amplitude,1e-10))+30)/30,-3,2))
    own=np.stack(descriptors,axis=2);extra=np.concatenate([own,own[:,::-1]],axis=2);extra=np.concatenate([np.full_like(extra[:1],-3),extra]).astype(np.float32)
    result={**d,'control':torch.cat([d['control'],torch.from_numpy(extra)],dim=2)};assert result['control'].shape[2]==61;return result


def prepare_source(source):return augment(previous_prepare(source))


class Model(PreviousModel):
    def __init__(self):super().__init__();self.controller=nn.GRU(61,48,batch_first=True)


def initialize(m):
    checkpoint=torch.load(ROOT/'surrogate-v20-continuous-selected.pt',weights_only=True);state=m.state_dict()
    for key,value in checkpoint['state_dict'].items():
        if key=='controller.weight_ih_l0':state[key].zero_();state[key][:,:29]=value
        else:state[key]=value
    m.load_state_dict(state);print('Warm start v20',checkpoint['step'],'new spectral descriptor weights zero',flush=True)


class Auxiliary(SeparatedIncrementalObjective):
    def __init__(self):super().__init__();self.data=[augment(d) for d in self.data]


def activate():
    core.VERSION='v23';core.EXTRA_TRAINING=['saturation-map','training-recovery','training-rich'];core.TRAINING_REPEATS={'training-rich':3};core.prepare_source=prepare_source;core.Model=Model
    core.LENGTH=32768;core.CONTEXT=24576;core.TARGET_TRANSFORM=target_transform


def check():
    torch.set_num_threads(2);torch.manual_seed(821);m=Model();initialize(m);old=PreviousModel();old.load_state_dict(torch.load(ROOT/'surrogate-v20-continuous-selected.pt',weights_only=True)['state_dict']);x=torch.randn(2,512,dtype=torch.float64);c=torch.randn(2,3,61);y=m(x,c);difference=float(torch.max(abs(y-old(x,c[:,:,:29]))).detach());assert difference<1e-7
    y.square().mean().backward();assert torch.isfinite(y).all() and all(p.grad is not None and torch.isfinite(p.grad).all() for p in m.parameters());assert not torch.count_nonzero(m(torch.zeros_like(x),c))
    rng=np.random.default_rng(821);raw=rng.normal(size=(32768,2));base=dict(x=raw,control=torch.zeros((129,2,29)));a=augment(base)['control'][:,:,29:];b=augment({**base,'x':-raw})['control'][:,:,29:];assert torch.equal(a,b)
    print('Initial difference',difference,'finite gradients, exact silence and polarity-invariant descriptors',flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['check','train']);parser.add_argument('--steps',type=int,default=16000);args=parser.parse_args()
    if args.mode=='check':check()
    else:
        plan=json.loads((ROOT/'selection-plan-v22.json').read_text());plan['candidate_family']='v23';plan['candidate_steps']=f'All completed 500-step checkpoints, 0 through {args.steps}';plan['expected_final_step']=args.steps;plan['spectral_descriptors']='Eight causal bandpass RMS and peak measurements, own/opposite channel';(ROOT/'selection-plan-v23.json').write_text(json.dumps(plan,indent=2))
        activate();core.train(args.steps,learning_rate=.0001,decay=True,initializer=initialize,retain_checkpoints=True,auxiliary_factory=Auxiliary)
