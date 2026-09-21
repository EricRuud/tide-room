"""Test the audio path with target-informed trajectories on training data only.

This is a diagnostic of representation, NOT an emulator accuracy score.
"""
import argparse,time
from torch import nn
from p821_900r2_common import *
from p821_dynamic_eq import Biquad
from p821_memory_limiter import Limiter
class FreeTrajectory(nn.Module):
 def __init__(self,m,v,c):
  super().__init__();self.filters=model(True)
  for p in self.filters.parameters():p.requires_grad_(False)
  self.values=nn.Parameter(v.clone());self.cap=nn.Parameter(c.clamp_min(.001).log());self.release=nn.Parameter(m.release.detach().clone())
 def forward(self,x):
  v=self.values.clamp(-30,30);f=torch.arange(1,257,dtype=torch.float64)[None,None,:]/256
  interp=lambda z:(z[:,:-1,None]*(1-f)+z[:,1:,None]*f).flatten(1,2)
  for c in self.filters.filter_coefficients(v):x=Biquad.apply(x,c)
  x=x*torch.pow(10.,interp(v[:,:,4])/20)
  return Limiter.apply(x,interp(self.cap.exp().clamp(.05,4)),torch.exp(-1/(SR*self.release.exp().clamp(.0001,.05))))
def main(steps,names):
 torch.set_num_threads(1);m=model();rows=[]
 for name in names:
  d=prepare(name);t=target(d);prediction=render(m,d);errors=np.mean((prediction-d['y'])**2,axis=1)
  # Choose a high-error training region; retain preceding filter context.
  length=131072;context=49152;starts=np.arange(0,len(errors)-length+1,256);c=np.r_[0,np.cumsum(errors)];energy=(c[starts+length]-c[starts+context]);a=int(starts[np.argmax(energy)]);b=a+length
  with torch.no_grad():v,cap=m.control_parts(d['control'].transpose(0,1))
  v=v[:,a//256:b//256+1];cap=cap[:,a//256:b//256+1];x=d['base'][a:b].T.double();y=torch.from_numpy(t[a:b].T.copy());fm=FreeTrajectory(m,v,cap)
  optimizer=torch.optim.Adam([{'params':[fm.values],'lr':.03},{'params':[fm.cap],'lr':.005},{'params':[fm.release],'lr':.0003}]);powers=y[:,context:].square().mean();log=[];start=time.monotonic()
  for step in range(steps+1):
   p=fm(x);loss=(p[:,context:]-y[:,context:]).square().mean()/powers
   if step%100==0 or step==steps:
    row={'step':step,'seconds':time.monotonic()-start,'residual_dbr':float(10*torch.log10(loss).detach())};log.append(row);print(name,row,flush=True)
   if step==steps:break
   curvature=fm.values[:,2:]-2*fm.values[:,1:-1]+fm.values[:,:-2];penalty=1e-6*curvature.square().mean()+1e-7*(fm.values-v).square().mean()
   total=loss+penalty;optimizer.zero_grad();total.backward();assert torch.isfinite(total);nn.utils.clip_grad_norm_([fm.values,fm.cap,fm.release],1);optimizer.step()
  np.savez_compressed(ROOT/f'trajectory-{name}.npz',values=fm.values.detach().numpy(),initial=v.numpy(),cap=fm.cap.exp().detach().numpy(),start=a,end=b,context=context)
  rows.append({'source':name,'start':a,'end':b,'context':context,'history':log});store('trajectory-diagnostic.json',{'target_informed':True,'not_emulator_score':True,'training_only':True,'rows':rows})
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--steps',type=int,default=400);p.add_argument('--names',nargs='+',default=['train-0','train-4','train-2','train-tone-0']);a=p.parse_args();main(a.steps,a.names)
