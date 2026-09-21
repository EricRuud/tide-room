"""Compare the selected 900 refinement and audit its unchanged native topology."""
import argparse,shutil,subprocess,tempfile
from p821_900r2_common import *
from p821_native_controls_check import export as controls
NAMES=['validation-0','validation-1','validation-2','validation-room','validation-history']

def candidate():
 m=RestModel();m.load_state_dict(torch.load(ROOT/'candidate-best.pt',weights_only=True)['state_dict']);m.eval();return m

def details(x,y,p):
 n=len(y)//256*256;x=x[:n];y=y[:n];p=p[:n]
 # These are repeatable energy-rise masks, not ground-truth note onsets.
 # Average 21 ms before detecting rises to reduce low-frequency carrier ripple.
 energy=np.sqrt(signal.lfilter(np.ones(4)/4,[1],np.mean(x.reshape(-1,256,2)**2,axis=(1,2))))
 rise=np.maximum(0,np.diff(energy,prepend=0))
 events=signal.find_peaks(rise,height=max(.0005,.12*rise.max()),distance=15)[0]*256
 events=events[(events>=SR)&(events+int(.2*SR)<n)]
 attack=np.zeros(n,dtype=bool);decay=np.zeros(n,dtype=bool)
 for a in events:
  attack[a:a+int(.03*SR)]=True;decay[a+int(.03*SR):a+int(.2*SR)]=True
 decay &= ~attack
 result={'overall':metrics(y[SR:],p[SR:]),'energy_rise_count':len(events)}
 for name,mask in [('first_30ms',attack),('following_30_to_200ms',decay)]:
  if mask.any():result[name]=metrics(y[mask],p[mask])
 for lo,hi in [(20,200),(200,2000),(2000,8000),(8000,20000)]:
  sos=signal.butter(2,[lo,hi],fs=SR,btype='bandpass',output='sos')
  result[f'band_{lo}_{hi}']=metrics(signal.sosfilt(sos,y,axis=0)[SR:],signal.sosfilt(sos,p,axis=0)[SR:])
 return result

def compare():
 torch.set_num_threads(1);m=candidate();old=model();rows={}
 for name in NAMES+['smoke-90030','slow-validation','final-new-material','train-edges']:
  if name=='smoke-90030':
   d=parent.prepare('smoke');d['y']=parent.read('smoke-90030')
  else:d=parent.prepare(name) if name=='final-new-material' else prepare(name)
  a=render(old,d);b=render(m,d);rows[name]={'parent':details(d['x'],d['y'],a),'candidate':details(d['x'],d['y'],b)}
  print(name,{k:round(v['overall']['residual_dbr'],3) for k,v in rows[name].items()},flush=True)
 # Both history-dependent filtering and neural state must settle harmlessly.
 silence=prepare_array(np.zeros((SR*4,2)));out=render(m,silence);assert np.count_nonzero(out)==0
 rows['silence_exact_zero']=True;store('validation-comparison.json',{'old_final_is_disclosed_validation':True,'input_only_onset_masks':True,'rows':rows})

@torch.no_grad()
def export():
 torch.set_num_threads(1);m=candidate();wm=parent.width_model(False);folder=ROOT/'native-model';folder.mkdir(exist_ok=True)
 controls(folder/'control.bin',m.state_dict(),wm.state_dict());(folder/'rest.bin').write_bytes(bytes([1]))
 for controller,size,index,name in [(m.controller,61,14,'fast'),(wm.controller,25,24,'width')]:
  quiet=torch.full((1,188,size),-3.);quiet[:,:,index]=-2.;_,state=torch.nn.GRU.forward(controller,quiet);state.numpy().astype('<f4').tofile(folder/f'quiet-{name}-state.bin')
 for name in ['base.bin','features.bin','slow.bin']:shutil.copyfile(PARENT/'native-model'/name,folder/name)
 with (folder/'dynamic-q.bin').open('wb') as f:
  for v,t in [(m.q_output.weight,'<f4'),(m.q_output.bias,'<f4'),(m.q_tau.exp(),'<f8')]:v.numpy().astype(t).tofile(f)
 np.r_[m.frequency.exp().numpy(),m.q.exp().numpy(),float(m.release.exp()),parent.matrix().reshape(-1),parent.width_scale()].astype('<f8').tofile(folder/'audio.bin')
 saved=torch.load(ROOT/'candidate-best.pt',weights_only=True)
 store('native-export.json',{'checkpoint':'candidate-best.pt','step':saved['step'],'sha256':digest(ROOT/'candidate-best.pt'),'files':{p.name:digest(p) for p in folder.glob('*.bin')},'topology_changed':False,'new_final_reference_used':False})

def native_check():
 torch.set_num_threads(1);m=candidate();rows={};folder=ROOT/'native-model'
 for name in NAMES:
  d=prepare(name);expected=render(m,d)
  with tempfile.TemporaryDirectory(prefix='tide-900r2-port-',dir='/private/tmp') as tmp:
   tmp=Path(tmp);d['x'].astype('<f4').tofile(tmp/'input.bin')
   command=[str(PARENT.parent/'p821-identification-002/native-lf-bin/tide-p821-lf-native'),str(folder),str(tmp/'input.bin'),str(tmp/'output.bin')]
   timing=json.loads(subprocess.check_output(command,text=True));actual=np.fromfile(tmp/'output.bin',dtype='<f8').reshape(-1,2)[:d['length']]
   parity=metrics(expected,actual);assert np.isfinite(actual).all() and parity['peak_error']<1e-5,parity
   rows[name]={'port':parity,'reference':metrics(d['y'][SR:],actual[SR:]),'benchmark':timing};print(name,rows[name],flush=True)
 store('native-port.json',rows)

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('mode',choices=['compare','export','native']);a=p.parse_args();{'compare':compare,'export':export,'native':native_check}[a.mode]()
