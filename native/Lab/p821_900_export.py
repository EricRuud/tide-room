"""Export our 900/30 weights and audit the C++ port against Python."""
import argparse,shutil,subprocess,tempfile
from p821_900_model import *
from p821_native_controls_check import export as controls

@torch.no_grad()
def export(checkpoint):
 torch.set_num_threads(1);saved=torch.load(ROOT/checkpoint,weights_only=True);m=RestModel();m.load_state_dict(saved['state_dict']);m.eval();wm=width_model(False)
 folder=ROOT/'native-model';folder.mkdir(exist_ok=True);controls(folder/'control.bin',m.state_dict(),wm.state_dict())
 (folder/'rest.bin').write_bytes(bytes([1]))
 for controller,size,index,name in [(m.controller,61,14,'fast'),(wm.controller,25,24,'width')]:
  quiet=torch.full((1,188,size),-3.);quiet[:,:,index]=-2.;_,state=torch.nn.GRU.forward(controller,quiet);state.numpy().astype('<f4').tofile(folder/f'quiet-{name}-state.bin')
 w=matrix();np.einsum('oa,ain->oin',np.linalg.inv(w),kernel()).astype('<f8').tofile(folder/'base.bin')
 shutil.copyfile(ROOT.parent/'p821-identification-002/native-lf-gated/features.bin',folder/'features.bin')
 with (folder/'dynamic-q.bin').open('wb') as f:
  for v,t in [(m.q_output.weight,'<f4'),(m.q_output.bias,'<f4'),(m.q_tau.exp(),'<f8')]:v.numpy().astype(t).tofile(f)
 np.r_[m.frequency.exp().numpy(),m.q.exp().numpy(),float(m.release.exp()),w.reshape(-1),width_scale()].astype('<f8').tofile(folder/'audio.bin')
 slow_parameters().astype('<f8').tofile(folder/'slow.bin')
 store('native-export.json',{'checkpoint':checkpoint,'step':saved['step'],'sha256':digest(ROOT/checkpoint),'files':{p.name:digest(p) for p in folder.glob('*.bin')},'target':'P821 1.5.0 / 900 / 30 ips / Full / CENTER off / neutral / motion-noise off','final_reference_used':False})
 return m,folder

def check(checkpoint):
 m,folder=export(checkpoint);wm=width_model();rows={}
 for name in ['validation-0','validation-1','validation-2','validation-room','validation-history']:
  d=prepare(name,wm);expected=render(m,d)
  with tempfile.TemporaryDirectory(prefix='tide-900-port-',dir='/private/tmp') as tmp:
   tmp=Path(tmp);d['x'].astype('<f4').tofile(tmp/'input.bin')
   cmd=[str(ROOT.parent/'p821-identification-002/native-lf-bin/tide-p821-lf-native'),str(folder),str(tmp/'input.bin'),str(tmp/'output.bin')]
   timing=json.loads(subprocess.check_output(cmd,text=True));actual=np.fromfile(tmp/'output.bin',dtype='<f8').reshape(-1,2)[:d['length']]
   parity=metrics(expected,actual);assert np.isfinite(actual).all() and parity['peak_error']<1e-5,parity
   rows[name]={'port':parity,'reference':metrics(d['y'][SR:],actual[SR:]),'benchmark':timing}
   print(name,rows[name],flush=True)
 store('native-port.json',rows)

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--checkpoint',default='refined-best.pt');p.add_argument('--check',action='store_true');a=p.parse_args();(check if a.check else export)(a.checkpoint)
