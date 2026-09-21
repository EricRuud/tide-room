"""900/30 calibration using our existing causal native-compatible model family."""
from pathlib import Path
import json,hashlib
import numpy as np
import soundfile as sf
from scipy import signal,optimize
import torch
from p821_identification_analysis import linear_render,metrics
from p821_slow_detector import smooth,predict_curve
from p821_state_seeded_model import Model as TrainingModel,Context
from p821_rest_state_model import Model as RestModel,reset_mask,peak_with_reset,reset_smoothing
from p821_settled_controller_model import SettledGRU
from p821_width_model import Model as WidthModel
from p821_stateful_render import predict as render
ROOT=Path(__file__).resolve().parents[2]/'artifacts/p821-90030-001';OLD=ROOT.parent/'p821-identification-001';SR=48000;BLOCK=256

def read(name):return sf.read(ROOT/(name+'.wav'),always_2d=True)[0]
def digest(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def store(name,value):(ROOT/name).write_text(json.dumps(value,indent=2))
def kernel():return np.load(ROOT/'quiet-kernel.npy')
def matrix():
 h=kernel();return h[:,:,0]/h[0,:,0].sum()
def features(x):
 x=np.asarray(x,dtype=np.float64);assert len(x)%BLOCK==0
 bank=np.fromfile(ROOT.parent/'p821-identification-002/native-lf-gated/features.bin',dtype='<f8').reshape(13,5)
 sources=[x]+[signal.lfilter(c[:3],np.r_[1,c[3:]],x,axis=0) for c in bank]
 rms=np.stack([np.sqrt(np.mean(z.reshape(-1,BLOCK,2)**2,axis=1)) for z in sources],axis=2)
 peak=np.stack([np.max(abs(z.reshape(-1,BLOCK,2)),axis=1)/np.sqrt(2) for z in sources],axis=2)
 enc=lambda v,upper=1:np.clip((20*np.log10(np.maximum(v,1e-10))+30)/30,-3,upper)
 ms=np.column_stack([x.mean(axis=1),(x[:,0]-x[:,1])*.5]).reshape(-1,BLOCK,2)
 spatial=enc(np.sqrt(np.mean(ms**2,axis=1)));spatial_peak=enc(np.max(abs(ms),axis=1)/np.sqrt(2))
 levels=np.mean(np.maximum(20*np.log10(np.maximum(rms[:,:,0],1e-10)),-90),axis=1);average=smooth(levels,47,0)
 f=np.zeros((len(rms),2,61),dtype=np.float32)
 f[:,:,:6]=enc(rms[:,:,:6]);f[:,:,6:12]=enc(rms[:,::-1,:6]);f[:,:,12:14]=spatial[:,None,:];f[:,:,14]=(average[:,None]+30)/30
 f[:,:,15:21]=enc(peak[:,:,:6]);f[:,:,21:27]=enc(peak[:,::-1,:6]);f[:,:,27:29]=spatial_peak[:,None,:]
 for k in range(8):
  f[:,:,29+2*k]=enc(rms[:,:,6+k],2);f[:,:,30+2*k]=enc(peak[:,:,6+k],2)
  f[:,:,45+2*k]=enc(rms[:,::-1,6+k],2);f[:,:,46+2*k]=enc(peak[:,::-1,6+k],2)
 initial=np.full((1,2,61),-3.,dtype=np.float32);initial[:,:,14]=(average[0]+30)/30;f=np.concatenate([initial,f])
 wf=np.column_stack([enc(rms[:,:,:6].min(axis=1)),enc(rms[:,:,:6].max(axis=1)),enc(peak[:,:,:6].min(axis=1)),enc(peak[:,:,:6].max(axis=1)),(average+30)/30,np.zeros(len(rms))]).astype(np.float32)
 # Match the existing native wrapper's virtual width frame (all -3 except gate).
 # Its fast-channel virtual frame above does use the first averaged level.
 initial=np.full((1,26),-3.,dtype=np.float32);initial[0,25]=0;wf=np.concatenate([initial,wf])
 mask=reset_mask(torch.from_numpy(wf)[None]);remembered=peak_with_reset(np.vstack([np.zeros((1,2)),rms[:,:,0]]).T,np.repeat(mask,2,axis=0));v=remembered.min(axis=0);wf[:,-1]=np.where(v>1e-5,v/(v+.003),0)
 return torch.from_numpy(f),torch.from_numpy(wf),levels

def slow_parameters():
 p=ROOT/'slow-fit.json';return np.asarray(json.loads(p.read_text())['parameters'] if p.exists() else [47,.0223,-45.1065,-.017,0.])
def slow_gain(levels,p):
 g=predict_curve(levels,p,'after');old=np.r_[p[4],g[:-1]];fraction=np.arange(1,257)[None,:]/256
 return 10**((old[:,None]*(1-fraction)+g[:,None]*fraction).reshape(-1)/20)
def width_scale():
 p=ROOT/'width-calibration.json';return json.loads(p.read_text())['full_scale'] if p.exists() else 1.
def width_model(settled=True):
 m=WidthModel();p=ROOT/'width-best.pt'
 if p.exists():m.load_state_dict(torch.load(p,weights_only=True)['state_dict'])
 else:
  with torch.no_grad():m.output.weight.zero_();m.output.bias.zero_()
 if settled:
  state=m.state_dict();m.controller=SettledGRU(25,32,batch_first=True,quiet_level_index=24);m.load_state_dict(state)
 m.eval();return m
@torch.no_grad()
def width_samples(m,wf):
 delta=m(wf[None])[0].numpy();mask=reset_mask(wf[None]);a=np.array([float(torch.exp(-256/(SR*m.tau.exp().clamp(.003,.2))))])
 delta=reset_smoothing(delta[None,:,None],mask,a,np.zeros((1,1)))[0,:,0]
 fraction=np.arange(1,257)[None,:]/256;db=(delta[:-1,None]*(1-fraction)+delta[1:,None]*fraction).reshape(-1)
 return 10**(db*width_scale()/20)
def prepare(name,wm=None):
 x=read('input-'+name);length=len(x);x=np.pad(x,((0,(-length)%256),(0,0)));f,wf,levels=features(x)
 w=matrix();base=linear_render(x,kernel())@np.linalg.inv(w).T;base*=slow_gain(levels,slow_parameters())[:,None]
 d={'name':name,'x':x.astype(np.float32),'base':torch.from_numpy(base.astype(np.float32)),'control':f,'width_features':wf,'w':w,'length':length,'extra_width_gain':width_samples(wm or width_model(),wf)}
 target=ROOT/(name+'.wav')
 if target.exists():d['y']=read(name)
 return d

def initial():
 m=TrainingModel();saved=torch.load(OLD/'surrogate-v31-continuous-selected.pt',weights_only=True);m.load_state_dict(saved['state_dict']);return m
@torch.no_grad()
def inference(m,d):
 r=RestModel();r.load_state_dict(m.state_dict());r.eval();return render(r,d)
def fit_slow():
 x=read('input-slow-levels');y=read('slow-levels');n=len(x)//256*256;x=x[:n];y=y[:n];_,_,levels=features(x)
 base=linear_render(x,kernel());osc=np.exp(-2j*np.pi*1500*np.arange(256)/SR)
 a=base[:,0].reshape(-1,256)@osc;b=y[:,0].reshape(-1,256)@osc;target=20*np.log10(np.maximum(abs(b/np.where(abs(a)>1e-10,a,1)),1e-20))
 use=(abs(a)>.05);use[:SR//256]=False
 def error(p):
  g=predict_curve(levels,np.r_[47,p],'after');# Mean interpolated block gain at the coherent tone.
  g=.5*(g+np.r_[p[-1],g[:-1]]);return (g-target)[use]
 result=optimize.least_squares(error,[.0223,-45.,-.017,0.],bounds=([.003,-70,-.2,-.3],[.2,-20,.2,.3]),max_nfev=300)
 p=np.r_[47,result.x];row={'parameters':p.tolist(),'training_gain_rmse_db':float(np.sqrt(np.mean(error(result.x)**2))),'method':'coherent 1500 Hz complex-amplitude gain relative to quiet FIR; 47-block log-RMS detector and following one-pole gain','validation':None};store('slow-fit.json',row)
 xv=read('input-slow-validation');yv=read('slow-validation');nv=len(xv)//256*256;xv=xv[:nv];yv=yv[:nv];_,_,lv=features(xv);prediction=linear_render(xv,kernel())*slow_gain(lv,p)[:,None];row['validation']=metrics(yv[SR:],prediction[SR:]);store('slow-fit.json',row);print(row,flush=True)
