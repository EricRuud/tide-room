"""Target-informed test of time-varying EQ bandwidth on synthetic training data.

This tests representational limits, not predictive accuracy or P821 internals.
All variants receive the same trajectory warm start and optimization budget.
"""
import argparse,json,tempfile,time
from pathlib import Path
import numpy as np
import soundfile as sf
import torch
from torch import nn
from p821_identification_analysis import ROOT,SR,metrics
from p821_spectral_control_model import Model,prepare_source
from p821_width_separated_model import sample_width
from p821_memory_trajectory import Trajectory
from p821_shelf_control_model import Model as ShelfModel


class ShapeFilters(ShelfModel):
    def __init__(self,initial,values,mode):
        super().__init__();self.mode=mode
        self.frequency.data.copy_(initial.frequency);self.q.data.copy_(initial.q)
        for p in self.parameters():p.requires_grad_(False)
        self.frequency.requires_grad_(True);self.q.requires_grad_(True)
        self.shape=nn.Parameter(torch.zeros_like(values[:,:,:4]) if mode=='free-q' else torch.zeros(4,dtype=torch.float64),requires_grad=mode!='global')

    def offsets(self,values):
        if self.mode=='global':return torch.zeros_like(values[:,:,:4])
        if self.mode=='free-q':return self.shape.clamp(-2,2)
        magnitude=torch.sqrt(values[:,:,:4]**2+.01)-.1
        return 2*torch.tanh(self.shape)*torch.tanh(magnitude/12)

    def filter_coefficients(self,values):
        offsets=self.offsets(values);result=[]
        for j in range(4):
            f=self.frequency[j].exp().clamp(20,18000)
            q=(self.q[j]+offsets[:,:,j]).exp().clamp(.2,8)
            A=torch.pow(10.,values[:,:,j]/40);omega=2*np.pi*f/SR;c=torch.cos(omega);alpha=torch.sin(omega)/(2*q)
            if j==0:
                beta=2*torch.sqrt(A)*alpha;a0=(A+1)+(A-1)*c+beta
                b0=A*((A+1)-(A-1)*c+beta)/a0;b1=2*A*((A-1)-(A+1)*c)/a0;b2=A*((A+1)-(A-1)*c-beta)/a0
                a1=-2*((A-1)+(A+1)*c)/a0;a2=((A+1)+(A-1)*c-beta)/a0
            else:
                a0=1+alpha/A;b0=(1+alpha*A)/a0;b1=-2*c/a0;b2=(1-alpha*A)/a0;a1=b1;a2=(1-alpha/A)/a0
            result.append(torch.stack([b0,b1,b2,a1,a2],dim=2))
        return result


def main(index,steps):
    torch.set_num_threads(1);folder=ROOT/'training-trajectories-v23'
    meta=json.loads((folder/f'case-{index:02d}.json').read_text());labels=np.load(folder/f'case-{index:02d}.npz');a,b=meta['start'],meta['end']
    source=json.loads((ROOT/'training-rich.json').read_text())['job']['input']
    with sf.SoundFile(source) as file:file.seek(a);raw=file.read(b-a,always_2d=True)
    with tempfile.TemporaryDirectory(prefix='p821-shapes-',dir='/private/tmp') as temp:
        path=Path(temp)/'source.wav';sf.write(path,raw,SR,subtype='FLOAT');d=prepare_source(path)
    with sf.SoundFile(ROOT/'training-rich.wav') as file:file.seek(a);target=file.read(b-a,always_2d=True)
    width=np.load(ROOT/'width-labels-training-rich.npz')['focus_delta_db'];gain=sample_width(width[a//256:b//256+1],b-a)
    mid=target.mean(axis=1);side=(target[:,0]-target[:,1])*.5/gain
    target=np.column_stack([mid+side,mid-side])@np.linalg.inv(d['w']).T
    initial=Model();initial.load_state_dict(torch.load(ROOT/'surrogate-v23-continuous-selected.pt',weights_only=True)['state_dict'])
    iv=torch.from_numpy(labels['values']);ic=torch.from_numpy(labels['cap']);x=d['base'].T.double();y=torch.from_numpy(target.T.copy())
    left,right=meta['case']['start']-a,meta['case']['end']-a;energy=y[:,left:right].square().mean();rows=[]
    baseline=Trajectory(initial,iv,ic)(x).detach()
    for mode in ['global','gain-dependent-q','free-q']:
        model=Trajectory(initial,iv,ic);model.filters=ShapeFilters(initial,iv,mode)
        initial_difference=float(torch.max(abs(model(x).detach()-baseline)));assert initial_difference<1e-10
        groups=[dict(params=[model.values,model.cap],lr=.01),dict(params=[model.filters.frequency,model.filters.q],lr=.0002)]
        if mode!='global':groups.append(dict(params=[model.filters.shape],lr=.003))
        rates=[group['lr'] for group in groups];optimizer=torch.optim.Adam(groups);parameters=[p for p in model.parameters() if p.requires_grad]
        cap0=model.cap.detach().clone();history=[];started=time.monotonic()
        for step in range(steps+1):
            prediction=model(x);error=(prediction[:,left:right]-y[:,left:right]).square().mean()/energy
            if step%100==0 or step==steps:
                entry=dict(step=step,seconds=time.monotonic()-started,residual=metrics(y[:,left:right].numpy(),prediction[:,left:right].detach().numpy()))
                history.append(entry);print(mode,step,round(entry['residual']['residual_dbr'],3),flush=True)
            if step==steps:break
            curvature=model.values[:,2:]-2*model.values[:,1:-1]+model.values[:,:-2]
            loss=error+1e-6*(model.values-iv).square().mean()+1e-6*curvature.square().mean()+1e-4*(model.cap-cap0).square().mean()
            if mode=='free-q':
                shape=model.filters.shape;loss=loss+1e-6*shape.square().mean()+1e-5*(shape[:,1:]-shape[:,:-1]).square().mean()
            optimizer.zero_grad();loss.backward();assert torch.isfinite(loss)
            nn.utils.clip_grad_norm_(parameters,1);optimizer.step()
            for group,rate in zip(optimizer.param_groups,rates):group['lr']=rate*(.1+.9*.5*(1+np.cos(np.pi*(step+1)/steps)))
        offsets=model.filters.offsets(model.values).detach().numpy()
        rows.append(dict(mode=mode,initial_difference=initial_difference,history=history,
                         frequency=model.filters.frequency.exp().detach().tolist(),base_q=model.filters.q.exp().detach().tolist(),
                         offset_min=offsets.min(axis=(0,1)).tolist(),offset_max=offsets.max(axis=(0,1)).tolist()))
        np.savez_compressed(ROOT/f'dynamic-shape-case-{index:02d}-{mode}.npz',values=model.values.detach().numpy(),cap=model.cap.exp().detach().numpy(),
                            frequency=model.filters.frequency.exp().detach().numpy(),q=model.filters.q.exp().detach().numpy(),shape=model.filters.shape.detach().numpy())
        (ROOT/f'dynamic-shape-case-{index:02d}.json').write_text(json.dumps(dict(training_case=index,steps=steps,rows=rows,
             target_informed=True,not_emulator_score=True,final_holdouts_used=False),indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--case',type=int,default=5);parser.add_argument('--steps',type=int,default=600)
    args=parser.parse_args();main(args.case,args.steps)
