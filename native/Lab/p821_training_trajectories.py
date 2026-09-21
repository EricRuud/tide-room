"""Fit latent control trajectories on synthetic TRAINING clips only.

These target-informed trajectories are non-unique optimization aids, not measured
P821 controls or emulator scores. Filter shapes and release are frozen to the
selected v23. Initial-control and curvature penalties discourage arbitrary use
of unobservable degrees of freedom. No validation/final audio is loaded.
"""
import argparse,hashlib,json,time
import numpy as np
import torch
from torch import nn
from p821_identification_analysis import ROOT,SR,read,metrics
from p821_spectral_control_model import Model,prepare_source
from p821_width_separated_model import strip_reference_width
from p821_memory_trajectory import Trajectory


def main(steps,minimum_rms):
    torch.set_num_threads(2);torch.manual_seed(821)
    checkpoint_path=ROOT/'surrogate-v23-continuous-selected.pt';checkpoint=torch.load(checkpoint_path,weights_only=True);initial=Model();initial.load_state_dict(checkpoint['state_dict']);initial.eval()
    name='training-rich';d=prepare_source(json.loads((ROOT/(name+'.json')).read_text())['job']['input']);target=strip_reference_width(name,read(name))@np.linalg.inv(d['w']).T
    with torch.no_grad():values,cap=initial.control_parts(d['control'].transpose(0,1))
    cases=[c for c in json.loads((ROOT/'training-rich-cases.json').read_text()) if c['actual_rms']>=minimum_rms];folder=ROOT/'training-trajectories-v23';folder.mkdir(exist_ok=True);rows=[];started=time.monotonic()
    plan=dict(target_informed_training_labels=True,non_unique_latent_controls=True,not_emulator_scores=True,validation_audio_used=False,final_holdouts_used=False,checkpoint_sha256=hashlib.sha256(checkpoint_path.read_bytes()).hexdigest(),checkpoint_step=checkpoint['step'],filter_shapes_and_release_frozen=True,steps=steps,minimum_rms=minimum_rms,case_indices=[c['index'] for c in cases],initial_penalty=1e-6,curvature_penalty=1e-6,log_ceiling_penalty=1e-4)
    plan_path=folder/'plan.json'
    if plan_path.exists():assert json.loads(plan_path.read_text())==plan,'Existing trajectory run uses a different plan'
    else:plan_path.write_text(json.dumps(plan,indent=2))
    for case in cases:
        destination=folder/f"case-{case['index']:02d}.npz";metadata=destination.with_suffix('.json')
        if destination.exists() and metadata.exists():rows.append(json.loads(metadata.read_text()));continue
        a=case['start']-SR;b=case['end']+SR;assert a%256==b%256==0;left=case['start']-a;right=case['end']-a
        x=d['base'][a:b].T.double();y=torch.from_numpy(target[a:b].T.copy());iv=values[:,a//256:b//256+1].clone();ic=cap[:,a//256:b//256+1].clone()
        model=Trajectory(initial,iv,ic)
        for p in model.filters.parameters():p.requires_grad_(False)
        initial_log_cap=model.cap.detach().clone();optimizer=torch.optim.Adam([model.values,model.cap],lr=.02);energy=torch.mean(y[:,left:right]**2);history=[];case_started=time.monotonic()
        for step in range(steps+1):
            prediction=model(x);loss=torch.mean((prediction[:,left:right]-y[:,left:right])**2)/energy
            if step%50==0 or step==steps:
                row=dict(step=step,seconds=time.monotonic()-case_started,target_informed_residual=metrics(y[:,left:right].numpy(),prediction[:,left:right].detach().numpy()));history.append(row);print('case',case['index'],'step',step,round(row['target_informed_residual']['residual_dbr'],3),'seconds',round(row['seconds'],1),flush=True)
            if step==steps:break
            curvature=model.values[:,2:]-2*model.values[:,1:-1]+model.values[:,:-2]
            total=loss+1e-6*torch.mean((model.values-iv)**2)+1e-6*torch.mean(curvature**2)+1e-4*torch.mean((model.cap-initial_log_cap)**2)
            optimizer.zero_grad();total.backward();nn.utils.clip_grad_norm_([model.values,model.cap],1);optimizer.step()
            for group in optimizer.param_groups:group['lr']=.02*(.1+.9*.5*(1+np.cos(np.pi*(step+1)/steps)))
        mask=np.zeros(model.values.shape[1],dtype=np.float32);mask[left//256:(right+255)//256]=1
        np.savez_compressed(destination,control=d['control'][a//256:b//256+1].numpy(),values=model.values.detach().numpy(),cap=np.exp(model.cap.detach().numpy()).clip(.65,3),initial_values=iv.numpy(),initial_cap=ic.numpy(),mask=mask)
        row=dict(case=case,start=a,end=b,history=history,total_seconds=time.monotonic()-case_started);metadata.write_text(json.dumps(row,indent=2));rows.append(row)
        (folder/'summary.json').write_text(json.dumps(dict(plan=plan,completed=rows,elapsed_seconds=time.monotonic()-started),indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--steps',type=int,default=250);parser.add_argument('--minimum-rms',type=float,default=.089);args=parser.parse_args();main(args.steps,args.minimum_rms)
