"""Target-informed diagnostic, NOT an emulator or independent validation.

Give three bell filters and gain arbitrary smooth trajectories. If even this
cannot reproduce a passage, fitting a detector for these filters is premature.
The target is used directly in optimization. No final holdout is opened.
"""
import argparse,json,time
import numpy as np
import torch
from torch import nn
from p821_identification_analysis import ROOT,SR,read,metrics
from p821_dynamic_eq import Biquad
import p821_adaptive_clip_model as source

BLOCK=256


class Trajectory(nn.Module):
    def __init__(self,frames):
        super().__init__()
        self.values=nn.Parameter(torch.zeros((2,frames,4),dtype=torch.float64))
        self.frequency=nn.Parameter(torch.tensor([180.,2620.,9925.],dtype=torch.float64).log())
        self.q=nn.Parameter(torch.tensor([1.8,.79,.52],dtype=torch.float64).log())
        self.cap=nn.Parameter(torch.full((2,frames),np.log(1.2),dtype=torch.float64))

    def forward(self,x):
        fraction=torch.arange(1,BLOCK+1,dtype=torch.float64)[None,None,:]/BLOCK
        interpolate=lambda c:(c[:,:-1,None]*(1-fraction)+c[:,1:,None]*fraction).flatten(1,2)
        values=torch.clamp(self.values,-30,30)
        for j in range(3):
            f=torch.clamp(torch.exp(self.frequency[j]),40,18000)
            q=torch.clamp(torch.exp(self.q[j]),.2,10)
            A=torch.pow(10.,values[:,:,j]/40);omega=2*np.pi*f/SR;alpha=torch.sin(omega)/(2*q);cosine=torch.cos(omega)
            a0=1+alpha/A;b0=(1+alpha*A)/a0;b1=-2*cosine/a0;b2=(1-alpha*A)/a0;a2=(1-alpha/A)/a0
            x=Biquad.apply(x,torch.stack([b0,b1,b2,b1,a2],dim=2))
        x=x*torch.pow(10.,interpolate(values[:,:,3])/20)
        cap=interpolate(torch.clamp(torch.exp(self.cap),.65,3))
        return torch.maximum(torch.minimum(x,cap),-cap)


def main(steps):
    torch.set_num_threads(2);torch.manual_seed(821)
    name='music-levels';job=json.loads((ROOT/(name+'.json')).read_text())['job']
    d=source.prepare_source(job['input']);y=read(name)@np.linalg.inv(d['w']).T
    case=json.loads((ROOT/'input-music-levels.json').read_text())[2]
    a=(case['start']-SR)//BLOCK*BLOCK;b=a+int(4*SR)//BLOCK*BLOCK;ignore=SR//BLOCK*BLOCK
    x=d['base'][a:b].T.double();target=torch.from_numpy(y[a:b].T)
    model=Trajectory(x.shape[1]//BLOCK+1)
    optimizer=torch.optim.Adam([{'params':[model.values,model.cap],'lr':.03},{'params':[model.frequency,model.q],'lr':.0002}])
    energy=torch.mean(target[:,ignore:]**2);rows=[];start=time.monotonic()
    for step in range(steps+1):
        yp=model(x);loss=torch.mean((yp[:,ignore:]-target[:,ignore:])**2)/energy
        if step%100==0:
            row=dict(step=step,seconds=time.monotonic()-start,target_informed_residual=metrics(target[:,ignore:].numpy(),yp[:,ignore:].detach().numpy()),frequencies=torch.exp(model.frequency).detach().tolist(),q=torch.exp(model.q).detach().tolist())
            rows.append(row);print(step,round(row['target_informed_residual']['residual_dbr'],2),row['frequencies'],row['q'],flush=True)
            (ROOT/'filter-trajectory-diagnostic.json').write_text(json.dumps(dict(warning='Uses target directly; not emulation or independent validation',source=name,start=a,end=b,ignored_samples=ignore,rows=rows),indent=2))
        if step==steps:break
        curvature=model.values[:,2:]-2*model.values[:,1:-1]+model.values[:,:-2]
        total=loss+1e-7*torch.mean(curvature**2)
        optimizer.zero_grad();total.backward();nn.utils.clip_grad_norm_(model.parameters(),1);optimizer.step()
        if step>0 and step%1000==0:
            for group in optimizer.param_groups:group['lr']*=.5
    np.savez_compressed(ROOT/'filter-trajectory-diagnostic.npz',values=model.values.detach().numpy(),frequency=torch.exp(model.frequency).detach().numpy(),q=torch.exp(model.q).detach().numpy(),cap=torch.exp(model.cap).detach().numpy())


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--steps',type=int,default=3000);main(parser.parse_args().steps)
