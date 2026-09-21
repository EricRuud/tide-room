"""Input-only 900 refinement, with onset sampling and a band-balanced loss.

All waveform supervision uses the existing training captures. The five older
validation recordings select the checkpoint; neither old nor new final trains it.
The causal model and native processing cost are unchanged.
"""
import argparse,time
from torch import nn
from p821_900r2_common import *
from p821_separated_model import atomic_write
LENGTH=49152;CONTEXT=32768
NAMES=['validation-0','validation-1','validation-2','validation-room','validation-history']
WEIGHTS=[.2,.2,.2,.3,.1]

def dataset(name):
 d=prepare(name);d['target']=torch.from_numpy(target(d).astype(np.float32));x=d['x']
 energy=signal.lfilter([1-np.exp(-1/(SR*.03))],[1,-np.exp(-1/(SR*.03))],x*x,axis=0)
 d['weight']=torch.tensor(1/np.sqrt(.015**2+energy),dtype=torch.float32)
 block_energy=np.mean(x.reshape(-1,256,2)**2,axis=(1,2));onset=np.maximum(0,np.diff(np.sqrt(block_energy),prepend=0))
 starts=np.arange(0,len(x)-LENGTH+1,256);cs=np.r_[0,np.cumsum(onset)];onsets=cs[(starts+LENGTH)//256]-cs[(starts+CONTEXT)//256]
 d['starts']=starts;prob=.5/len(starts)+.5*onsets/max(onsets.sum(),1e-20);d['probability']=prob/prob.sum()
 for key in ['x','y','extra_width_gain','width_features']:d.pop(key,None)
 return d

def main(steps,prefix='',edges=False,learning_rate=.00008,spectral_weight=.15):
 assert not (ROOT/'final-freeze.json').exists()
 torch.set_num_threads(1);torch.manual_seed(903201);rng=np.random.default_rng(903201)
 training=[dataset(f'train-{i}') for i in range(8)]+[dataset(f'train-tone-{i}') for i in range(2)]
 if edges:training.append(dataset('train-edges'))
 validation=[prepare(n) for n in NAMES];m=model(True);context=Context(interval=100)
 parameters=list(m.parameters());optimizer=torch.optim.Adam(parameters,lr=learning_rate)
 parent_scores=json.loads((PARENT/'tonal-best.json').read_text())['validation'];best=sum(parent_scores[n]['residual_dbr']*w for n,w in zip(NAMES,WEIGHTS))
 log=[];started=time.monotonic()
 store(prefix+'training-plan.json',{'training':[d['name'] for d in training],'validation':NAMES,'seed':903201,'steps':steps,'initial_checkpoint':digest(PARENT/'tonal-best.pt'),'loss':'Relative waveform error with 30 ms RMS weighting (floor .015), plus relative first-difference error; half uniform and half input-onset weighted sampling; .683 s context followed by .341 s scored windows.','spectral_weight':spectral_weight,'learning_rate':learning_rate,'selection_weights':dict(zip(NAMES,WEIGHTS)),'guard_db':{'room':.25,'each_other':.75},'topology_changed':False,'final_material_used':False})
 for step in range(steps+1):
  if step%400==0 or step==steps:
   r=RestModel();r.load_state_dict(m.state_dict());r.eval();scores={d['name']:metrics(d['y'][SR:],render(r,d)[SR:]) for d in validation}
   score=sum(scores[n]['residual_dbr']*w for n,w in zip(NAMES,WEIGHTS))
   admissible=all(scores[n]['residual_dbr']<=parent_scores[n]['residual_dbr']+(.25 if n=='validation-room' else .75) for n in NAMES)
   row={'step':step,'seconds':time.monotonic()-started,'score':score,'admissible':admissible,'validation':scores};log.append(row);store(prefix+'training.json',log)
   checkpoint={'state_dict':m.state_dict(),'step':step}
   if score<best and admissible:
    best=score;atomic_write(ROOT/(prefix+'candidate-best.pt'),lambda p:torch.save(checkpoint,p));store(prefix+'candidate-best.json',row)
   atomic_write(ROOT/(prefix+'candidate-last.pt'),lambda p:torch.save(checkpoint,p))
   print(step,'score',round(score,3),'admissible',admissible,{n:round(v['residual_dbr'],3) for n,v in scores.items()},'seconds',round(time.monotonic()-started,1),flush=True)
  if step==steps:break
  examples=[]
  for j in range(4):
   extra=[0,1,4,5,8,9]+([10,10,10,10] if edges else [])
   i=(step+j)%(10+len(extra));i=i if i<10 else extra[i-10];d=training[i];a=int(rng.choice(d['starts'],p=d['probability']));examples.append((d,a,int(rng.integers(0,2))))
  x=torch.stack([d['base'][a:a+LENGTH,c] for d,a,c in examples]);control=torch.stack([d['control'][a//256:(a+LENGTH)//256+1,c] for d,a,c in examples]);y=torch.stack([d['target'][a+CONTEXT:a+LENGTH,c] for d,a,c in examples]);weight=torch.stack([d['weight'][a+CONTEXT:a+LENGTH,c] for d,a,c in examples])
  context.before(m,training,examples,step)
  try:p=m(x,control)[:,CONTEXT:]
  finally:context.after(m)
  error=(p-y)*weight;wave=error.square().mean()
  dy=y[:,1:]-y[:,:-1];de=(p[:,1:]-p[:,:-1])-dy
  spectral=(de.square().mean(1)/(dy.square().mean(1)+.002**2)).mean()
  loss=wave+spectral_weight*spectral;assert torch.isfinite(loss)
  optimizer.zero_grad();loss.backward();assert all(p.grad is None or torch.isfinite(p.grad).all() for p in parameters)
  nn.utils.clip_grad_norm_(parameters,1);optimizer.step()
  rate=learning_rate*(.2+.8*.5*(1+np.cos(np.pi*(step+1)/steps)))
  for group in optimizer.param_groups:group['lr']=float(rate)
  if step%100==99:print('step',step+1,'loss',float(loss.detach()),'seconds',round(time.monotonic()-started,1),flush=True)

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--steps',type=int,default=4000);p.add_argument('--prefix',default='');p.add_argument('--edges',action='store_true');p.add_argument('--rate',type=float,default=.00008);p.add_argument('--spectral',type=float,default=.15);a=p.parse_args();main(a.steps,a.prefix,a.edges,a.rate,a.spectral)
