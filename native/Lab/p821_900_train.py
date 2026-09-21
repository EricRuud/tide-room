"""Train only on new 900/30 captures; select using independent validation clips."""
import argparse,time,json
import numpy as np
import torch
from scipy import signal
from p821_900_model import *
from p821_900_width import label
from p821_separated_model import atomic_write
LENGTH=32768;CONTEXT=24576

def dataset(name,wm):
 d=prepare(name,wm);y=d['y'];labels=np.load(ROOT/f'width-labels-{name}.npz')['delta'];f=np.arange(1,257)[None,:]/256
 db=(labels[:-1,None]*(1-f)+labels[1:,None]*f).reshape(-1)*width_scale();g=np.pad(10**(db/20),(0,max(0,len(y)-len(db))),constant_values=1)[:len(y)]
 mid=y.mean(axis=1);side=(y[:,0]-y[:,1])*.5/g;target=np.column_stack([mid+side,mid-side])@np.linalg.inv(d['w']).T;target=np.pad(target,((0,len(d['x'])-len(target)),(0,0)))
 d['target']=torch.tensor(target,dtype=torch.float32)
 energy=signal.lfilter([1-np.exp(-1/(SR*.03))],[1,-np.exp(-1/(SR*.03))],d['x']**2,axis=0);d['weight']=torch.tensor(1/np.sqrt(.01**2+energy),dtype=torch.float32)
 starts=np.arange(0,len(d['base'])-LENGTH+1,256);c=np.r_[0,np.cumsum(np.mean(d['x']**2,axis=1),dtype=np.float64)];a=(c[starts+LENGTH]-c[starts+CONTEXT])/(LENGTH-CONTEXT);prob=.5/len(starts)+.5*a/max(a.sum(),1e-30);d['starts']=starts;d['probability']=prob/prob.sum()
 for key in ['x','y','extra_width_gain','width_features']:d.pop(key,None)
 return d

def main(steps,resume=None,tonal=False):
 assert not (ROOT/'final-freeze.json').exists(),'This experiment is frozen. Use a new experiment directory for further fitting.'
 torch.set_num_threads(1);torch.manual_seed(903001);rng=np.random.default_rng(903001);wm=width_model()
 training=[dataset(f'train-{i}',wm) for i in range(8)];validation=[prepare(n,wm) for n in ['validation-0','validation-1','validation-2','validation-room','validation-history']]
 m=initial();parameters=[p for p in m.parameters() if p.requires_grad];optimizer=torch.optim.Adam(parameters,lr=.0003);context=Context(interval=100);start_step=0;log=[];prefix='model';best=1e9
 if resume:
  # This local checkpoint was written by this training script and includes NumPy RNG state.
  saved=torch.load(ROOT/resume,weights_only=False);m.load_state_dict(saved['state_dict']);start_step=saved['step'];optimizer.load_state_dict(saved['optimizer']);rng.bit_generator.state=saved['numpy_rng'];torch.set_rng_state(saved['torch_rng']);prefix='refined'
 if tonal:
  assert not resume
  m.load_state_dict(torch.load(ROOT/'refined-best.pt',weights_only=True)['state_dict']);prefix='tonal'
  baseline=json.loads((ROOT/'width-calibration.json').read_text())['difference_db']
  for i in range(2):
   name=f'train-tone-{i}';v,w,_=label(read(name),read(name+'-focus-zero'),baseline);np.savez_compressed(ROOT/f'width-labels-{name}.npz',delta=v,weight=w);training.append(dataset(name,wm))
 store(prefix+'-plan.json',{'steps':steps,'resume':resume,'training':[d['name'] for d in training],'validation':[d['name'] for d in validation],'loss':'weighted waveform error after .512 s of filter/controller context; four channel windows; full-input GRU cache refreshed every 100 steps','selection_weights':{'validation-0':.2,'validation-1':.2,'validation-2':.2,'validation-room':.3,'validation-history':.1},'final_reference_used':False,'new_seed':903001,'preserved_456_checkpoint':str(OLD/'surrogate-v31-continuous-selected.pt')})
 started=time.monotonic()
 for step in range(start_step,steps+1):
  if step%200==0 or step==steps or step==start_step:
   m.eval();scores={}
   for d in validation:scores[d['name']]=metrics(d['y'][SR:],inference(m,d)[SR:])
   score=sum(scores[n]['residual_dbr']*w for n,w in [('validation-0',.2),('validation-1',.2),('validation-2',.2),('validation-room',.3),('validation-history',.1)])
   row={'step':step,'seconds':time.monotonic()-started,'score':score,'validation':scores,'frequencies':m.frequency.exp().detach().tolist(),'q':m.q.exp().detach().tolist()};log.append(row);store(prefix+'-training.json',log)
   checkpoint={'state_dict':m.state_dict(),'step':step};atomic_write(ROOT/f'{prefix}-step-{step}.pt',lambda p:torch.save(checkpoint,p))
   if score<best:best=score;atomic_write(ROOT/f'{prefix}-best.pt',lambda p:torch.save(checkpoint,p));store(prefix+'-best.json',row)
   resumable=checkpoint|{'optimizer':optimizer.state_dict(),'numpy_rng':rng.bit_generator.state,'torch_rng':torch.get_rng_state()};atomic_write(ROOT/f'{prefix}-resume.pt',lambda p:torch.save(resumable,p))
   print(prefix,step,'score',round(score,3),{n:round(v['residual_dbr'],3) for n,v in scores.items()},'seconds',round(time.monotonic()-started,1),flush=True);m.train()
  if step==steps:break
  examples=[]
  for j in range(4):
   # Extra normal-level windows; highest drive is retained as a stress minority.
   i=(step+j)%12;i=i if i<8 else ([8,9,8,9][i-8] if tonal else [0,1,4,5][i-8]);d=training[i];a=int(rng.choice(d['starts'],p=d['probability']));examples.append((d,a,int(rng.integers(0,2))))
  x=torch.stack([d['base'][a:a+LENGTH,c] for d,a,c in examples]);control=torch.stack([d['control'][a//256:(a+LENGTH)//256+1,c] for d,a,c in examples]);target=torch.stack([d['target'][a+CONTEXT:a+LENGTH,c] for d,a,c in examples]);weight=torch.stack([d['weight'][a+CONTEXT:a+LENGTH,c] for d,a,c in examples])
  context.before(m,training,examples,step)
  try:prediction=m(x,control)[:,CONTEXT:]
  finally:context.after(m)
  loss=(((prediction-target)*weight)**2).mean();assert torch.isfinite(loss),('Nonfinite loss',step)
  optimizer.zero_grad();loss.backward();assert all(p.grad is None or torch.isfinite(p.grad).all() for p in parameters);torch.nn.utils.clip_grad_norm_(parameters,1);optimizer.step()
  rate=(.0001 if tonal else .0003)*(.1+.9*.5*(1+np.cos(np.pi*(step-start_step+1)/max(1,steps-start_step))))
  for group in optimizer.param_groups:group['lr']=float(rate)
  if step%50==49:print('step',step+1,'loss',float(loss.detach()),'seconds',round(time.monotonic()-started,1),flush=True)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--steps',type=int,default=2000);p.add_argument('--resume');p.add_argument('--tonal',action='store_true');a=p.parse_args();main(a.steps,a.resume,a.tonal)
