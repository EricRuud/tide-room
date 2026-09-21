"""v13: slow averaging, faster dynamic EQ, and an adaptive output clipping stage.

The ceiling hypothesis comes from the transient/steady and one-channel tests.
Its base-rate hard clip is an identification experiment; aliasing must be measured
and controlled before any production use. No exact P821 architecture is claimed.
"""
import argparse,json
import numpy as np
import torch
from torch import nn
from torchaudio.functional import lfilter
import p821_separated_model as core
from p821_slow_detector import smooth,predict_curve,gain_signal
from p821_identification_analysis import ROOT,SR

original_prepare=core.prepare_source


def prepare_source(source):
    d=original_prepare(source);x=d['x'].astype(np.float64)
    rms=np.sqrt(np.mean(x.reshape(-1,256,2)**2,axis=1))
    # The waveform ceiling begins to move earlier after silence than a -200 dB
    # detector floor would predict. -90 dB is a hypothesis to validate, not a
    # recovered constant. The averaging duration is identified separately.
    levels=np.mean(np.maximum(20*np.log10(np.maximum(rms,1e-10)),-90),axis=1)
    row=json.loads((ROOT/'slow-detector-fit.json').read_text())[1];p=np.array(list(row['parameters'].values()))
    dbgain=predict_curve(levels,p,'after')
    fraction=np.arange(1,257)[None,:]/256
    old=np.concatenate([[p[4]],dbgain[:-1]])
    newgain=10**(((old[:,None]*(1-fraction)+dbgain[:,None]*fraction).reshape(-1))/20)
    d['base']*=torch.from_numpy((newgain/gain_signal(x)).astype(np.float32))[:,None]
    average=smooth(levels,47,0);average=np.concatenate([[average[0]],average])
    extra=np.repeat(((average+30)/30)[:,None,None],2,axis=1).astype(np.float32)
    d['control']=torch.cat([d['control'],torch.from_numpy(extra)],dim=2)
    return d


class Model(core.Model):
    def __init__(self):
        super().__init__()
        self.bass_threshold=18.;self.bass_slope=.9
        with torch.no_grad():self.frequency[0]=np.log(180);self.q[0]=np.log(1.8)
        self.ceiling=nn.Parameter(torch.tensor(np.log(.795),dtype=torch.float64))
        self.growth=nn.Parameter(torch.tensor([np.log(.014),np.log(.0002)],dtype=torch.float64))
        self.ceiling_tau=nn.Parameter(torch.tensor(np.log(.005),dtype=torch.float64))

    def forward_parts(self,x,control):
        y=super().forward(x,control[:,:,:14])
        level=torch.relu(control[:,:,14].double()*30-30+45.1065)
        growth=torch.exp(self.growth)
        cap=torch.clamp(torch.exp(self.ceiling),.6,1)+growth[0]*level+growth[1]*level*level
        tau=torch.clamp(torch.exp(self.ceiling_tau),.001,.1);alpha=torch.exp(-256/(SR*tau))
        a=torch.stack([torch.ones_like(alpha),-alpha]);b=torch.stack([1-alpha,torch.zeros_like(alpha)])
        cap=lfilter(cap,b_coeffs=b,a_coeffs=a,clamp=False)
        fraction=torch.arange(1,257,dtype=torch.float64)[None,None,:]/256
        cap=(cap[:,:-1,None]*(1-fraction)+cap[:,1:,None]*fraction).flatten(1,2)
        return y,cap

    def forward(self,x,control):
        y,cap=self.forward_parts(x,control)
        return torch.maximum(torch.minimum(y,cap),-cap)


def activate():
    core.VERSION='v13';core.EXTRA_TRAINING=['saturation-map'];core.prepare_source=prepare_source;core.Model=Model


def check():
    torch.set_num_threads(2);torch.manual_seed(821)
    model=Model();x=torch.randn(2,512,dtype=torch.float64)*2;c=torch.zeros((2,3,15));c[:,:,0]=1;c[:,:,2]=.8;c[:,:,12]=.5;c[:,:,14]=.1
    y=model(x,c);y.square().mean().backward()
    assert torch.isfinite(y).all() and all(p.grad is not None and torch.isfinite(p.grad).all() for p in model.parameters())
    assert not torch.count_nonzero(model(torch.zeros_like(x),c))
    print('Adaptive clip forward/backward finite, silence preserved',flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['check','train']);parser.add_argument('--steps',type=int,default=10000)
    args=parser.parse_args()
    if args.mode=='check':check()
    else:activate();core.train(args.steps)
