"""Measure and fit 900/30's small Focus-dependent width movement."""
import time,json
import numpy as np
import torch
from scipy.linalg import solve_banded
from p821_900_model import *

def label(full,zero,baseline):
 n=min(len(full),len(zero))//256*256;a=(full[:n,0]-full[:n,1])*.5;b=(zero[:n,0]-zero[:n,1])*.5;frames=n//256
 energy=np.mean(b.reshape(-1,256)**2,axis=1);weight=b*b/np.repeat(np.maximum(energy,1e-12),256);weight*=np.repeat(energy>1e-8,256);weight*=((abs(b)>1e-7)&(a*b>0))
 target=np.clip(20*np.log10(np.maximum(abs(a),1e-30)/np.maximum(abs(b),1e-30)),-5,5)
 r=np.tile(np.arange(1,257)/256,frames);l=1-r;diag=np.zeros(frames+1);rhs=np.zeros(frames+1)
 diag[:-1]+=np.sum((weight*l*l).reshape(-1,256),axis=1);diag[1:]+=np.sum((weight*r*r).reshape(-1,256),axis=1);cross=np.sum((weight*l*r).reshape(-1,256),axis=1)
 rhs[:-1]+=np.sum((weight*l*target).reshape(-1,256),axis=1);rhs[1:]+=np.sum((weight*r*target).reshape(-1,256),axis=1)
 diag[:-1]+=.01;diag[1:]+=.01;cross-=.01;diag+=.001;rhs+=.001*baseline
 bands=np.zeros((3,frames+1));bands[1]=diag;bands[0,1:]=cross;bands[2,:-1]=cross;values=solve_banded((1,1),bands,rhs)
 interpolation=(values[:-1,None]*(1-np.arange(1,257)/256)+values[1:,None]*np.arange(1,257)/256).reshape(-1)
 return values-baseline,np.r_[energy[0],energy],{'mid_residual_dbr':metrics(full[:n].mean(axis=1),zero[:n].mean(axis=1))['residual_dbr'],'side_fit_dbr':metrics(a,b*10**(interpolation/20))['residual_dbr']}

def main():
 torch.set_num_threads(1);torch.manual_seed(903030);rng=np.random.default_rng(903030)
 h=kernel();z=read('impulse-zero');hz=np.stack([z[SR:SR+32768].T/.001,z[int(3.5*SR):int(3.5*SR)+32768].T/.001],axis=1)
 gain=lambda h:float(20*np.log10(abs((h[0,0,0]-h[0,1,0])/(h[0,0,0]+h[0,1,0]))))
 full,zero=gain(h),gain(hz);baseline=full-zero;scale=full/baseline
 store('width-calibration.json',{'full_db':full,'off_db':zero,'difference_db':baseline,'full_scale':scale,'note':'Static matrix plus input-predicted dynamic correction; Focus factorization hypothesis checked against full waveform.'})
 data={};rows={}
 for family,count in [('train',8),('validation',3)]:
  for i in range(count):
   name=f'{family}-{i}';x=read('input-'+name);x=np.pad(x,((0,(-len(x))%256),(0,0)));_,wf,_=features(x);v,w,row=label(read(name),read(name+'-focus-zero'),baseline)
   k=min(len(v),len(wf));v=v[:k];w=w[:k];wf=wf[:k];np.savez_compressed(ROOT/f'width-labels-{name}.npz',delta=v,weight=w)
   observed=w>1e-8;row.update(delta_min=float(v[observed].min()),delta_max=float(v[observed].max()));rows[name]=row
   data[name]={'features':wf,'target':torch.tensor(v),'weight':torch.tensor(observed,dtype=torch.float64)}
 store('width-factorization.json',rows);print(rows,flush=True)
 m=WidthModel();m.output.bias.data.zero_();optimizer=torch.optim.Adam(m.parameters(),lr=.001);log=[];best=1e9;started=time.monotonic()
 for step in range(501):
  if step%100==0:
   m.eval();scores={}
   with torch.no_grad():
    for name,d in data.items():
     if name.startswith('validation'):
      pred=m(d['features'][None])[0];scores[name]=float(torch.sqrt(((pred-d['target'])**2*d['weight']).sum()/d['weight'].sum()))
   row={'step':step,'seconds':time.monotonic()-started,'rmse_db':scores};log.append(row);store('width-training.json',log)
   score=np.mean(list(scores.values()));print(row,flush=True)
   if score<best:best=score;torch.save({'state_dict':m.state_dict(),'step':step},ROOT/'width-best.pt')
   m.train()
  if step==500:break
  examples=[]
  for j in range(4):
   d=data[f'train-{(step+j)%8}'];a=int(rng.integers(0,len(d['target'])-256));examples.append((d,a))
  f=torch.stack([d['features'][a:a+256] for d,a in examples]);target=torch.stack([d['target'][a+192:a+256] for d,a in examples]);w=torch.stack([d['weight'][a+192:a+256] for d,a in examples]);pred=m(f)[:,192:];loss=((pred-target)**2*w).sum()/w.sum().clamp_min(1)
  optimizer.zero_grad();loss.backward();torch.nn.utils.clip_grad_norm_(m.parameters(),1);optimizer.step()
if __name__=='__main__':main()
