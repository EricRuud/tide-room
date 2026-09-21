"""v16: peak descriptors added after the equal-RMS crest-history experiment.

Warm starts the v15 controller with zero weights on new inputs, preserving its
initial prediction up to float32 matrix-accumulation differences. Fine tuning remains input-only on the original
training set; crest-history is diagnostic validation and final holdouts stay out.
"""
import argparse
import numpy as np
import torch
from torch import nn
from scipy import signal
import p821_separated_model as core
from p821_shelf_control_model import Model as ShelfModel
from p821_adaptive_clip_model import prepare_source as previous_prepare
from p821_identification_analysis import ROOT,SR


def prepare_source(source):
    d=previous_prepare(source);x=d['x'].astype(np.float64);signals=[x]
    for f,kind in [(100,'lowpass'),(300,'lowpass'),(1000,'lowpass'),(3000,'highpass'),(8000,'highpass')]:
        signals.append(signal.sosfilt(signal.butter(2,f,btype=kind,fs=SR,output='sos'),x,axis=0))
    def descriptor(v):
        peak=np.max(abs(v.reshape(-1,256,2)),axis=1)/np.sqrt(2)
        return np.clip((20*np.log10(np.maximum(peak,1e-8))+30)/30,-3,1)
    own=np.stack([descriptor(s) for s in signals],axis=2)
    ms=np.column_stack([x.mean(axis=1),(x[:,0]-x[:,1])*.5]);spatial=descriptor(ms)
    extra=np.concatenate([own,own[:,::-1],np.repeat(spatial[:,None,:],2,axis=1)],axis=2)
    extra=np.concatenate([np.full_like(extra[:1],-3),extra]).astype(np.float32)
    d['control']=torch.cat([d['control'],torch.from_numpy(extra)],dim=2)
    return d


class Model(ShelfModel):
    def __init__(self):
        super().__init__();self.controller=nn.GRU(29,48,batch_first=True)


def initialize(model):
    checkpoint=torch.load(ROOT/'surrogate-v15-best.pt',weights_only=True);old=checkpoint['state_dict'];new=model.state_dict()
    for key,value in old.items():
        if key=='controller.weight_ih_l0':new[key].zero_();new[key][:,:15]=value
        else:new[key]=value
    model.load_state_dict(new)
    print('Warm start from v15 step',checkpoint['step'],'new descriptor weights zero',flush=True)


def activate():
    core.VERSION='v16';core.EXTRA_TRAINING=['saturation-map'];core.prepare_source=prepare_source;core.Model=Model


def check():
    torch.set_num_threads(2);torch.manual_seed(821)
    m=Model();initialize(m);old=ShelfModel();old.load_state_dict(torch.load(ROOT/'surrogate-v15-best.pt',weights_only=True)['state_dict'])
    x=torch.randn(2,512,dtype=torch.float64)*2;c=torch.randn((2,3,29))*.5;c[:,:,0]=.7;c[:,:,14]=.1
    y=m(x,c);expected=old(x,c[:,:,:15]);difference=float(torch.max(abs(y-expected)).detach());assert difference<1e-7
    y.square().mean().backward()
    assert torch.isfinite(y).all() and all(p.grad is not None and torch.isfinite(p.grad).all() for p in m.parameters())
    assert not torch.count_nonzero(m(torch.zeros_like(x),c))
    print('Expanded controller initial maximum difference',difference,'gradients finite; silence zero',flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['check','train']);parser.add_argument('--steps',type=int,default=8000);args=parser.parse_args()
    if args.mode=='check':check()
    else:activate();core.train(args.steps,learning_rate=.0002,decay=True,initializer=initialize)
