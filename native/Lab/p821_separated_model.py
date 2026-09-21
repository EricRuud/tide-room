"""v12: identified slow shared detector plus learned faster filter controls.

The slow component is fitted separately. A small controller adjusts three bell
filters and gain; its output is smoothed, so smoothing occurs after the nonlinear
control map. Only input-derived information is used by predict/prepare_source.
This is a fixed-setting offline hypothesis, not P821's disclosed architecture.
"""
import argparse,hashlib,json,os,tempfile,time
from pathlib import Path
import numpy as np
import torch
from torch import nn
from scipy import signal
from torchaudio.functional import lfilter
from p821_identification_analysis import ROOT,SR,read,linear_kernel,linear_render,metrics
from p821_bandlimited_dictionary import raw_input
from p821_slow_detector import gain_signal
from p821_dynamic_eq import Biquad

BLOCK=256;LENGTH=16384;CONTEXT=12288
VERSION='v12'
EXTRA_TRAINING=[]
TARGET_TRANSFORM=None
TRAINING_REPEATS={}


def controls(x):
    signals=[x]
    for frequency,kind in [(100,'lowpass'),(300,'lowpass'),(1000,'lowpass'),(3000,'highpass'),(8000,'highpass')]:
        signals.append(signal.sosfilt(signal.butter(2,frequency,btype=kind,fs=SR,output='sos'),x,axis=0))
    features=[]
    for source in signals:
        rms=np.sqrt(np.mean(source.reshape(-1,BLOCK,2)**2,axis=1))
        features.append(np.clip((20*np.log10(np.maximum(rms,1e-8))+30)/30,-3,1))
    own=np.stack(features,axis=2)
    mid=x.mean(axis=1);side=(x[:,0]-x[:,1])*.5
    spatial=np.stack([np.sqrt(np.mean(s.reshape(-1,BLOCK)**2,axis=1)) for s in [mid,side]],axis=1)
    spatial=np.clip((20*np.log10(np.maximum(spatial,1e-8))+30)/30,-3,1)
    result=np.concatenate([own,own[:,::-1],np.repeat(spatial[:,None,:],2,axis=1)],axis=2)
    return torch.from_numpy(np.concatenate([np.full_like(result[:1],-3),result],axis=0).astype(np.float32))


def prepare_source(source):
    x=raw_input(source);length=len(x);x=np.pad(x,((0,(-length)%BLOCK),(0,0)))
    h=linear_kernel();w=h[:,:,0]/(h[0,0,0]+h[0,1,0])
    base=linear_render(x,h)@np.linalg.inv(w).T
    base*=gain_signal(x)[:,None]
    return dict(x=x.astype(np.float32),base=torch.from_numpy(base.astype(np.float32)),control=controls(x),w=w,length=length)


def dataset(name):
    job=json.loads((ROOT/(name+'.json')).read_text())['job'];d=prepare_source(job['input'])
    y=read(name);y=np.pad(y,((0,len(d['x'])-len(y)),(0,0)))
    d['name']=name;d['y']=y.astype(np.float32);d['target']=torch.from_numpy((y@np.linalg.inv(d['w']).T).astype(np.float32))
    if TARGET_TRANSFORM is not None:d['target']=TARGET_TRANSFORM(name,y,d)
    energy=signal.lfilter([1-np.exp(-1/(SR*.03))],[1,-np.exp(-1/(SR*.03))],d['x']**2,axis=0)
    d['weight']=torch.tensor(1/np.sqrt(.01**2+energy),dtype=torch.float32)
    starts=np.arange(0,len(y)-LENGTH+1,BLOCK)
    cumulative=np.concatenate([[0],np.cumsum(np.mean(d['x']**2,axis=1),dtype=np.float64)])
    activity=(cumulative[starts+LENGTH]-cumulative[starts+CONTEXT])/(LENGTH-CONTEXT)
    probability=.25/len(starts)+.75*activity/max(np.sum(activity),1e-30)
    d['starts']=starts;d['probability']=probability/probability.sum()
    print('Prepared',name,len(y)/SR,'seconds',flush=True)
    return d


def validation_dataset(name):
    """Inference needs source features and raw targets, not training caches."""
    job=json.loads((ROOT/(name+'.json')).read_text())['job'];d=prepare_source(job['input'])
    d['name']=name;d['y']=read(name).astype(np.float32)
    print('Prepared validation',name,d['length']/SR,'seconds',flush=True)
    return d


class Model(nn.Module):
    def __init__(self):
        super().__init__()
        self.controller=nn.Sequential(nn.Linear(14,32),nn.Tanh(),nn.Linear(32,32),nn.Tanh(),nn.Linear(32,4))
        nn.init.zeros_(self.controller[-1].weight);nn.init.zeros_(self.controller[-1].bias)
        self.frequency=nn.Parameter(torch.tensor([160.,2620.,9925.],dtype=torch.float64).log())
        self.q=nn.Parameter(torch.tensor([.95,.79,.52],dtype=torch.float64).log())
        self.tau=nn.Parameter(torch.full((4,),np.log(.0223),dtype=torch.float64))
        self.bass_threshold=12.
        self.bass_slope=.83

    def forward(self,x,control):
        x=x.double()
        # Channels 0/2 are the own full/low RMS; 6 is other RMS; 12 is mid RMS.
        db=control.double()*30-30
        level=torch.relu(db[:,:,0]+12)
        low=torch.relu(db[:,:,2]+self.bass_threshold)
        mid=torch.relu(db[:,:,12]+12)
        initial=torch.stack([self.bass_slope*low,-.74*level,-.44*level,.20*level-.08*mid],dim=2)
        own_rms=torch.pow(10.,db[:,:,0]/20);other_rms=torch.pow(10.,db[:,:,6]/20)
        activity=torch.clamp((own_rms-.1)/(.1+own_rms),0,1)
        shared=torch.clamp((torch.maximum(own_rms,other_rms)-.1)/(.1+torch.maximum(own_rms,other_rms)),0,1)
        gate=torch.stack([activity,activity,activity,shared],dim=2)
        values=torch.clamp(initial+6*torch.tanh(self.controller(control).double())*gate,-15,15)
        tau=torch.clamp(torch.exp(self.tau),.003,.2)
        alpha=torch.exp(-BLOCK/(SR*tau));zeros=torch.zeros_like(alpha);ones=torch.ones_like(alpha)
        a=torch.stack([ones,-alpha],dim=1);b=torch.stack([1-alpha,zeros],dim=1)
        values=lfilter(values.transpose(1,2),a,b,clamp=False).transpose(1,2)
        fraction=torch.arange(1,BLOCK+1,dtype=torch.float64)[None,None,:]/BLOCK
        interpolate=lambda c:(c[:,:-1,None]*(1-fraction)+c[:,1:,None]*fraction).flatten(1,2)
        for j in range(3):
            f=torch.clamp(torch.exp(self.frequency[j]),40,18000)
            q=torch.clamp(torch.exp(self.q[j]),.3,3)
            A=torch.pow(10.,values[:,:,j]/40);omega=2*np.pi*f/SR;alpha=torch.sin(omega)/(2*q);cosine=torch.cos(omega)
            a0=1+alpha/A;b0=(1+alpha*A)/a0;b1=-2*cosine/a0;b2=(1-alpha*A)/a0;a2=(1-alpha/A)/a0
            x=Biquad.apply(x,torch.stack([b0,b1,b2,b1,a2],dim=2))
        return x*torch.pow(10.,interpolate(values[:,:,3])/20)


@torch.no_grad()
def predict(model,d):
    if getattr(model,'continuous_render',False):
        from p821_stateful_render import predict as continuous
        return continuous(model,d)
    return predict_contextual(model,d)


@torch.no_grad()
def predict_contextual(model,d):
    output=np.zeros_like(d['x'],dtype=np.float64)
    for pos in range(0,len(output),32768):
        a=max(0,pos-32768);b=min(len(output),pos+32768)
        y=model(d['base'][a:b].T,d['control'][a//BLOCK:b//BLOCK+1].transpose(0,1)).T.numpy()
        output[pos:b]=y[pos-a:]
    return (output@d['w'].T)[:d['length']]


def atomic_write(path,writer):
    """A failed save must leave the previous checkpoint/log intact."""
    path=Path(path);fd,name=tempfile.mkstemp(prefix=path.name+'.',suffix='.tmp',dir=path.parent)
    os.close(fd)
    try:
        writer(Path(name));os.replace(name,path)
    finally:
        if os.path.exists(name):os.unlink(name)


def save_json(path,value):
    atomic_write(path,lambda temporary:temporary.write_text(json.dumps(value,indent=2)))


def train(steps,learning_rate=.0005,decay=False,initializer=None,retain_checkpoints=False,auxiliary_factory=None,parameter_groups_factory=None,resume_from=None,batch_context_factory=None):
    torch.set_num_threads(2);torch.manual_seed(25821);rng=np.random.default_rng(25821)
    # Audio/model inputs and normalized targets are already prepared. Keeping
    # the raw source, raw target and inference-only width arrays here duplicated
    # hundreds of MB without serving any operation in the training loop.
    released_bytes=0;training=[]
    for name in ['training-noise','envelope-plus','training-patterns','structure-left','structure-mono','structure-side','training-grid','long-levels']+EXTRA_TRAINING:
        d=dataset(name)
        for key in ['x','y','extra_width_gain']:
            value=d.pop(key,None)
            if value is not None:released_bytes+=value.nbytes
        training.append(d)
    del value
    print('Released unused training arrays MiB',round(released_bytes/1024**2,1),flush=True)
    # Repeat dataset references, not waveform/feature preparation or storage.
    training=[d for d in training for _ in range(TRAINING_REPEATS.get(d['name'],1))]
    validation=[validation_dataset(n) for n in ['music-levels','history-plus','quality-tones','detector-pulses']]
    model=Model()
    if initializer is not None:initializer(model)
    auxiliary=auxiliary_factory() if auxiliary_factory is not None else None
    parameters=parameter_groups_factory(model) if parameter_groups_factory is not None else model.parameters()
    optimizer=torch.optim.Adam(parameters,lr=learning_rate)
    batch_context=batch_context_factory() if batch_context_factory is not None else None
    log=[];best=float('inf');start_step=0;elapsed_offset=0.
    log_path=ROOT/f'surrogate-{VERSION}-training.json'
    if resume_from is not None:
        resume_path=Path(resume_from);saved=torch.load(resume_path,weights_only=True)
        model.load_state_dict(saved['state_dict']);start_step=int(saved['step'])
        assert 0<=start_step<steps,(start_step,steps)
        log=[row for row in json.loads(log_path.read_text()) if row['step']<=start_step]
        assert log and log[-1]['step']==start_step,'Resume requires a matching completed validation row'
        elapsed_offset=log[-1]['seconds'];best=min(np.mean([v['residual_dbr'] for v in row['validation'].values()]) for row in log)
        exact=all(key in saved for key in ['optimizer','numpy_rng','torch_rng'])
        if exact:
            optimizer.load_state_dict(saved['optimizer']);rng.bit_generator.state=saved['numpy_rng'];torch.set_rng_state(saved['torch_rng'])
        else:
            # Legacy checkpoints contain weights only. Reproduce data sampling;
            # Adam's moments cannot be recovered and explicitly restart.
            for previous_step in range(start_step):
                for i in range(6):
                    d=training[(previous_step+i)%len(training)]
                    rng.choice(d['starts'],p=d['probability']);rng.integers(0,2)
        event=dict(step=start_step,source=str(resume_path),sha256=hashlib.sha256(resume_path.read_bytes()).hexdigest(),
                   unix_time=time.time(),optimizer_reset=not exact,numpy_sampling_replayed=not exact,
                   exact_training_state_restored=exact,total_steps=steps,learning_rate=learning_rate,decay=decay)
        resume_log=ROOT/f'surrogate-{VERSION}-resumes.json'
        events=json.loads(resume_log.read_text()) if resume_log.exists() else []
        save_json(resume_log,events+[event]);print('Resuming',event,flush=True)
    start=time.monotonic()
    for step in range(start_step,steps+1):
        if decay:
            rate=learning_rate*(.05+.95*.5*(1+np.cos(np.pi*step/max(steps,1))))
            for group in optimizer.param_groups:group['lr']=rate*group.get('lr_scale',1.)
        if step%500==0 and not (resume_from is not None and step==start_step):
            model.eval()
            row=dict(step=step,seconds=elapsed_offset+time.monotonic()-start,validation={d['name']:metrics(d['y'][:d['length']][SR:],predict(model,d)[SR:]) for d in validation},
                     frequencies=torch.exp(model.frequency).detach().tolist(),q=torch.exp(model.q).detach().tolist(),tau=torch.exp(model.tau).detach().tolist())
            log.append(row)
            checkpoint=dict(state_dict=model.state_dict(),step=step)
            if retain_checkpoints:atomic_write(ROOT/f'surrogate-{VERSION}-step-{step}.pt',lambda p:torch.save(checkpoint,p))
            atomic_write(ROOT/f'surrogate-{VERSION}.pt',lambda p:torch.save(checkpoint,p))
            score=np.mean([v['residual_dbr'] for v in row['validation'].values()])
            if score<best:
                best=score;atomic_write(ROOT/f'surrogate-{VERSION}-best.pt',lambda p:torch.save(checkpoint,p))
            save_json(log_path,log)
            resumable=checkpoint|dict(optimizer=optimizer.state_dict(),numpy_rng=rng.bit_generator.state,torch_rng=torch.get_rng_state())
            atomic_write(ROOT/f'surrogate-{VERSION}-resume.pt',lambda p:torch.save(resumable,p))
            print(step,{k:round(v['residual_dbr'],2) for k,v in row['validation'].items()},'seconds',round(time.monotonic()-start,1),flush=True)
            model.train()
        if step==steps:break
        selected=[training[(step+i)%len(training)] for i in range(6)]
        examples=[(d,int(rng.choice(d['starts'],p=d['probability'])),int(rng.integers(0,2))) for d in selected]
        x=torch.stack([d['base'][a:a+LENGTH,c] for d,a,c in examples])
        controls=torch.stack([d['control'][a//BLOCK:(a+LENGTH)//BLOCK+1,c] for d,a,c in examples])
        y=torch.stack([d['target'][a+CONTEXT:a+LENGTH,c] for d,a,c in examples])
        weight=torch.stack([d['weight'][a+CONTEXT:a+LENGTH,c] for d,a,c in examples])
        if batch_context is not None:batch_context.before(model,training,examples,step)
        try:prediction=model(x,controls)[:,CONTEXT:]
        finally:
            if batch_context is not None:batch_context.after(model)
        loss=torch.mean(((prediction-y)*weight)**2)
        if auxiliary is not None and step%4==0:loss=loss+auxiliary(model,step)
        optimizer.zero_grad();loss.backward();nn.utils.clip_grad_norm_(model.parameters(),1);optimizer.step()
        if step%100==99:print('step',step+1,'loss',float(loss.detach()),'seconds',round(time.monotonic()-start,1),flush=True)


def check():
    torch.set_num_threads(2);torch.manual_seed(821)
    model=Model();x=torch.randn(2,512,dtype=torch.float64)*.3;c=torch.zeros((2,3,14));c[:,:,0]=1;c[:,:,2]=.8;c[:,:,12]=.5
    y=model(x,c);loss=(y*y).mean();loss.backward()
    assert np.isfinite(y.detach().numpy()).all()
    assert all(p.grad is not None and torch.isfinite(p.grad).all() for p in model.parameters())
    assert torch.count_nonzero(model(torch.zeros_like(x),c))==0
    print('Finite forward/backward across the detector and filters; silence preserved',flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['check','train']);parser.add_argument('--steps',type=int,default=6000)
    args=parser.parse_args();check() if args.mode=='check' else train(args.steps)
