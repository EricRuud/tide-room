"""v21: give the controller explicit input-derived slow detector states.

Long gain recovery occurs mainly in heavily driven low-frequency tests. A small
bank of RMS/peak memories makes those histories visible without requiring the
GRU to infer every long time constant from short training windows. This remains
a behavioral hypothesis; no reference response supplies inference features.
"""
import argparse,json
import numpy as np
import torch
from torch import nn
from scipy import signal
from p821_identification_analysis import ROOT,SR
from p821_slow_detector import smooth
from p821_history_gate_model import leaky_maximum
from p821_width_separated_model import Model as PreviousModel,prepare_source as previous_prepare,SeparatedIncrementalObjective,target_transform
import p821_separated_model as core


def augment(d):
    x=d['x'].astype(np.float64);sources=[x]
    for f,kind in [(300,'lowpass'),(3000,'highpass')]:sources.append(signal.sosfilt(signal.butter(2,f,btype=kind,fs=SR,output='sos'),x,axis=0))
    rms=[];peak=[]
    for source in sources:
        blocks=source.reshape(-1,256,2);rms.append(np.sqrt(np.mean(blocks**2,axis=1)));peak.append(np.max(abs(blocks),axis=1)/np.sqrt(2))
    def exponential(value,tau):
        alpha=np.exp(-256/(SR*tau));return np.sqrt(np.maximum(signal.lfilter([1-alpha],[1,-alpha],value**2,axis=0),0))
    def maximum(value,tau):return leaky_maximum(value.T.copy(),np.exp(-256/(SR*tau))).T
    encode=lambda value:np.clip((20*np.log10(np.maximum(value,1e-10))+30)/30,-3,1)
    average=np.column_stack([smooth(np.maximum(20*np.log10(np.maximum(rms[0][:,c],1e-10)),-90),47,0) for c in range(2)])
    values=[(average+30)/30]
    values.extend(encode(exponential(rms[0],tau)) for tau in [.08,.25])
    values.extend(encode(maximum(peak[0],tau)) for tau in [.08,.25,.75])
    for j,tau in [(1,.25),(2,.08)]:values.extend([encode(exponential(rms[j],tau)),encode(maximum(peak[j],tau))])
    own=np.stack(values,axis=2);extra=np.concatenate([own,own[:,::-1]],axis=2);extra=np.concatenate([np.full_like(extra[:1],-3),extra]).astype(np.float32)
    result={**d,'control':torch.cat([d['control'],torch.from_numpy(extra)],dim=2)}
    assert result['control'].shape[2]==49
    return result


def prepare_source(source):return augment(previous_prepare(source))


class Model(PreviousModel):
    def __init__(self):super().__init__();self.controller=nn.GRU(49,48,batch_first=True)


def initialize(m):
    checkpoint=torch.load(ROOT/'surrogate-v19-selected.pt',weights_only=True);state=m.state_dict()
    for key,value in checkpoint['state_dict'].items():
        if key=='controller.weight_ih_l0':state[key].zero_();state[key][:,:29]=value
        else:state[key]=value
    m.load_state_dict(state);print('Warm start v19',checkpoint['step'],'new state features initially ignored',flush=True)


class Auxiliary(SeparatedIncrementalObjective):
    def __init__(self):super().__init__();self.data=[augment(d) for d in self.data]


def activate():
    core.VERSION='v21';core.EXTRA_TRAINING=['saturation-map','training-recovery'];core.prepare_source=prepare_source;core.Model=Model
    core.LENGTH=32768;core.CONTEXT=24576;core.TARGET_TRANSFORM=target_transform


def check():
    torch.set_num_threads(2);torch.manual_seed(821);m=Model();initialize(m);old=PreviousModel();old.load_state_dict(torch.load(ROOT/'surrogate-v19-selected.pt',weights_only=True)['state_dict'])
    x=torch.randn(2,512,dtype=torch.float64);c=torch.randn(2,3,49);expected=old(x,c[:,:,:29]);y=m(x,c);difference=float(torch.max(abs(y-expected)).detach());assert difference<1e-7
    y.square().mean().backward();assert torch.isfinite(y).all() and all(p.grad is not None and torch.isfinite(p.grad).all() for p in m.parameters());assert not torch.count_nonzero(m(torch.zeros_like(x),c))
    print('Initial difference',difference,'finite parameter gradients, silence zero',flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['check','train']);parser.add_argument('--steps',type=int,default=10000);args=parser.parse_args()
    if args.mode=='check':check()
    else:
        plan=json.loads((ROOT/'selection-plan-v20.json').read_text());plan['candidate_family']='v21';plan['candidate_steps']=f'All completed 500-step checkpoints, 0 through {args.steps}';(ROOT/'selection-plan-v21.json').write_text(json.dumps(plan,indent=2))
        activate();core.train(args.steps,learning_rate=.0002,decay=True,initializer=initialize,retain_checkpoints=True,auxiliary_factory=Auxiliary)
