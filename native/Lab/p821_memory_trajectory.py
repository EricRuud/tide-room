"""Target-informed structural ceiling for the four-filter/memory-limiter family.

Arbitrary control trajectories directly fit an already-used music excerpt. This
is NOT an emulator, an independent null, or data for final model selection. It
tests whether controller prediction or the audio-path structure limits accuracy.
"""
import argparse,json,time
import numpy as np
import torch
from torch import nn
from p821_identification_analysis import ROOT,SR,read,metrics
from p821_width_separated_model import prepare_source,strip_reference_width
from p821_history_gate_model import Model as InitialModel
from p821_shelf_control_model import Model as ShelfModel
from p821_dynamic_eq import Biquad
from p821_memory_limiter import Limiter


class Trajectory(nn.Module):
    def __init__(self,initial,values,cap):
        super().__init__();self.filters=ShelfModel();self.filters.frequency.data.copy_(initial.frequency);self.filters.q.data.copy_(initial.q)
        self.values=nn.Parameter(values.clone());self.cap=nn.Parameter(cap.clamp_min(.001).log());self.register_buffer('alpha',torch.exp(-1/(SR*torch.exp(initial.release.detach()))))

    def forward(self,x):
        values=torch.clamp(self.values,-30,30);fraction=torch.arange(1,257,dtype=torch.float64)[None,None,:]/256
        interpolate=lambda c:(c[:,:-1,None]*(1-fraction)+c[:,1:,None]*fraction).flatten(1,2)
        for coefficients in self.filters.filter_coefficients(values):x=Biquad.apply(x,coefficients)
        x=x*torch.pow(10.,interpolate(values[:,:,4])/20)
        return Limiter.apply(x,interpolate(torch.exp(self.cap).clamp(.65,3)),self.alpha)


def main(steps):
    torch.set_num_threads(2);torch.manual_seed(821);source=json.loads((ROOT/'music-levels.json').read_text())['job']['input'];d=prepare_source(source);target=strip_reference_width('music-levels',read('music-levels'))@np.linalg.inv(d['w']).T
    checkpoint=torch.load(ROOT/'surrogate-v20-continuous-selected.pt',weights_only=True);initial=InitialModel();initial.load_state_dict(checkpoint['state_dict']);initial.eval()
    with torch.no_grad():values,cap=initial.control_parts(d['control'].transpose(0,1))
    case=json.loads((ROOT/'input-music-levels.json').read_text())[2];a=(case['start']-SR)//256*256;b=a+(4*SR)//256*256;ignore=SR//256*256;x=d['base'][a:b].T.double();y=torch.tensor(target[a:b].T)
    m=Trajectory(initial,values[:,a//256:b//256+1],cap[:,a//256:b//256+1]);opt=torch.optim.Adam([{'params':[m.values,m.cap],'lr':.015},{'params':[m.filters.frequency,m.filters.q],'lr':.0001}]);energy=torch.mean(y[:,ignore:]**2);rows=[];started=time.monotonic()
    for step in range(steps+1):
        pred=m(x);loss=torch.mean((pred[:,ignore:]-y[:,ignore:])**2)/energy
        if step%100==0:
            row=dict(step=step,seconds=time.monotonic()-started,target_informed_residual=metrics(y[:,ignore:].numpy(),pred[:,ignore:].detach().numpy()),frequencies=torch.exp(m.filters.frequency).detach().tolist(),q=torch.exp(m.filters.q).detach().tolist());rows.append(row);print(step,round(row['target_informed_residual']['residual_dbr'],3),flush=True)
            (ROOT/'memory-trajectory-diagnostic.json').write_text(json.dumps(dict(target_informed=True,not_emulator_score=True,source='music-levels',start=a,end=b,ignored_samples=ignore,rows=rows),indent=2))
        if step==steps:break
        curvature=m.values[:,2:]-2*m.values[:,1:-1]+m.values[:,:-2];total=loss+1e-8*torch.mean(curvature**2);opt.zero_grad();total.backward();nn.utils.clip_grad_norm_([m.values,m.cap,m.filters.frequency,m.filters.q],1);opt.step()
        if step>0 and step%750==0:
            for group in opt.param_groups:group['lr']*=.5
    np.savez_compressed(ROOT/'memory-trajectory-diagnostic.npz',values=m.values.detach().numpy(),frequency=torch.exp(m.filters.frequency).detach().numpy(),q=torch.exp(m.filters.q).detach().numpy(),cap=torch.exp(m.cap).detach().numpy())


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--steps',type=int,default=1500);main(parser.parse_args().steps)
