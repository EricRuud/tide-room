"""Test DC rejection before the fast controller, retaining the slow input branch.

Quiet-pilot/DC measurements reject the existing model's large sustained fast
EQ response to constant offsets. These are input-only placement hypotheses,
not a recovered P821 detector filter. No model weights are fitted by this audit.
"""
import argparse,importlib,json
import numpy as np
import torch
from scipy import signal
from p821_identification_analysis import ROOT,SR,read,metrics
from p821_separated_model import controls
from p821_select_continuous import MODULES


def fast_controls(x,slow):
    x=x.astype(np.float64);c15=torch.cat([controls(x),slow],dim=2);signals=[x]
    for f,kind in [(100,'lowpass'),(300,'lowpass'),(1000,'lowpass'),(3000,'highpass'),(8000,'highpass')]:signals.append(signal.sosfilt(signal.butter(2,f,btype=kind,fs=SR,output='sos'),x,axis=0))
    def descriptor(v):return np.clip((20*np.log10(np.maximum(np.max(abs(v.reshape(-1,256,2)),axis=1)/np.sqrt(2),1e-8))+30)/30,-3,1)
    own=np.stack([descriptor(s) for s in signals],axis=2);spatial=descriptor(np.column_stack([x.mean(axis=1),(x[:,0]-x[:,1])*.5]));extra=np.concatenate([own,own[:,::-1],np.repeat(spatial[:,None,:],2,axis=1)],axis=2);extra=np.concatenate([np.full_like(extra[:1],-3),extra]).astype(np.float32)
    return torch.cat([c15,torch.from_numpy(extra)],dim=2)


def change(d,cutoff=5.,order=2):
    x=signal.sosfilt(signal.butter(order,cutoff,btype='highpass',fs=SR,output='sos'),d['x'],axis=0) if cutoff>0 else d['x']
    c=fast_controls(x,d['control'][:,:,14:15])
    if d['control'].shape[2]==61:
        from p821_spectral_control_model import augment
        c=augment(dict(x=x,control=c))['control']
    return {**d,'control':c}


def main(version):
    extension=importlib.import_module(MODULES[version]);extension.activate();core=extension.core;torch.set_num_threads(2);checkpoint=torch.load(ROOT/f'surrogate-{version}-continuous-selected.pt',weights_only=True);m=core.Model();m.load_state_dict(checkpoint['state_dict']);m.eval();rows={}
    for name in ['music-levels','history-plus','quality-dc','quality-tones','dc-detector-probe-plus','dc-detector-probe-minus']:
        d=core.prepare_source(json.loads((ROOT/(name+'.json')).read_text())['job']['input']);target=read(name);unchanged=change(d,0);assert torch.equal(unchanged['control'],d['control'])
        variants={'raw':d,'hp1_5':change(d,5,1),'hp2_5':change(d,5,2),'hp2_2':change(d,2,2)};result={}
        for label,data in variants.items():
            y=core.predict(m,data);row=metrics(target[SR:],y[SR:]);row['peak']=float(np.max(abs(y)));row['finite']=bool(np.isfinite(y).all())
            if name=='music-levels':row['passages']=[c|metrics(target[c['start']:c['end']],y[c['start']:c['end']]) for c in json.loads((ROOT/'input-music-levels.json').read_text())]
            result[label]=row
        rows[name]=result;print(name,{k:round(v['residual_dbr'],3) for k,v in result.items()},flush=True)
        if name=='music-levels':print('passages',{k:[round(v['residual_dbr'],3) for v in row['passages']] for k,row in result.items()},flush=True)
        (ROOT/f'detector-placement-{version}.json').write_text(json.dumps(dict(version=version,step=checkpoint['step'],no_weights_refitted=True,final_holdouts_used=False,base_and_slow_branch_unchanged=True,results=rows),indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('version',choices=list(MODULES));main(parser.parse_args().version)
