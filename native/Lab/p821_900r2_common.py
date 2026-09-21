"""Read the frozen first 900 experiment; write only into the second one."""
from pathlib import Path
import json,hashlib
import numpy as np
import torch
import soundfile as sf
from scipy import signal
import p821_900_model as parent
from p821_900_model import SR,BLOCK,metrics,RestModel,TrainingModel,Context
from p821_stateful_render import predict as render,mix
ROOT=Path(__file__).resolve().parents[2]/'artifacts/p821-90030-002'
PARENT=parent.ROOT

def store(name,value):
 (ROOT/name).write_text(json.dumps(value,indent=2))
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def model(training=False):
 m=TrainingModel() if training else RestModel();m.load_state_dict(torch.load(PARENT/'tonal-best.pt',weights_only=True)['state_dict']);m.train(training);return m
def prepare_array(x,name='generated',y=None):
 x=np.asarray(x,dtype=np.float64);length=len(x);x=np.pad(x,((0,(-length)%256),(0,0)))
 f,wf,levels=parent.features(x);w=parent.matrix()
 base=parent.linear_render(x,parent.kernel())@np.linalg.inv(w).T
 base*=parent.slow_gain(levels,parent.slow_parameters())[:,None]
 d={'name':name,'x':x.astype(np.float32),'base':torch.from_numpy(base.astype(np.float32)),'control':f,'width_features':wf,'w':w,'length':length,'extra_width_gain':parent.width_samples(parent.width_model(),wf)}
 if y is not None:d['y']=y
 return d
def prepare(name):
 if (ROOT/f'input-{name}.wav').exists():
  x,sr=sf.read(ROOT/f'input-{name}.wav',always_2d=True);assert sr==SR
  p=ROOT/f'{name}.wav';y=sf.read(p,always_2d=True)[0] if p.exists() else None
  return prepare_array(x,name,y)
 return parent.prepare(name)
def target(d):
 name=d['name'];y=d['y'];folder=ROOT if (ROOT/f'width-labels-{name}.npz').exists() else PARENT
 labels=np.load(folder/f'width-labels-{name}.npz')['delta'];f=np.arange(1,257)[None,:]/256
 db=(labels[:-1,None]*(1-f)+labels[1:,None]*f).reshape(-1)*parent.width_scale();g=np.pad(10**(db/20),(0,max(0,len(y)-len(db))),constant_values=1)[:len(y)]
 mid=y.mean(axis=1);side=(y[:,0]-y[:,1])*.5/g;result=np.column_stack([mid+side,mid-side])@np.linalg.inv(d['w']).T
 return np.pad(result,((0,len(d['x'])-len(result)),(0,0)))
