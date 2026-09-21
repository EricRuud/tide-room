"""No-refit comparison of moving filter realizations on development data.

SVF equations follow Andrew Simper's public trapezoidal SVF derivation:
https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf
The same stationary response does not imply the same modulation behavior.
"""
import argparse,json,time
import numpy as np
from numba import njit
from scipy import signal
import torch
from p821_identification_analysis import ROOT,SR,read,metrics
from p821_select_continuous import MODULES
from p821_select_staged import paired_shape
from p821_stateful_render import parts,mix
from p821_memory_limiter import render as limit
import importlib


from p821_svf_filters import moving_filter

@torch.no_grad()
def render(model,d,svf):
    values,cap=model.control_parts(d['control'].transpose(0,1));values=values.numpy();cap=cap.numpy();x=d['base'].double().numpy().copy()
    if values.shape[2]>5:offsets=values[:,:,5:9]
    elif hasattr(model,'offsets'):offsets=model.offsets(torch.from_numpy(values)).numpy()
    else:offsets=np.zeros_like(values[:,:,:4])
    f=model.frequency.exp().clamp(20,18000).numpy();q=model.q.exp().numpy()
    for j in range(4):x=moving_filter(x,values[:,:,j],offsets[:,:,j],float(f[j]),float(q[j]),j==0,svf)
    fraction=np.arange(1,257)[None,:,None]/256
    gains=(values[:,:-1,4].T[:,None,:]*(1-fraction)+values[:,1:,4].T[:,None,:]*fraction).reshape(-1,2)
    ceiling=(cap[:,:-1].T[:,None,:]*(1-fraction)+cap[:,1:].T[:,None,:]*fraction).reshape(-1,2)
    return mix(limit(x*10**(gains/20),ceiling,float(model.release.exp()),1),d)


def check():
    from p821_shelf_control_model import Model
    torch.set_num_threads(1);rng=np.random.default_rng(822);model=Model();x=rng.normal(size=(8192,2));maximum=0.;count=0
    for frequency in [20.,66.,2500.,18000.]:
        for q in [.2,.7,8.]:
            for gain in [-30.,0.,30.]:
                for shelf in [False,True]:
                    values=torch.full((2,33,5),gain,dtype=torch.float64)
                    with torch.no_grad():model.frequency.fill_(np.log(frequency));model.q.fill_(np.log(q));coeff=model.filter_coefficients(values)[0 if shelf else 1].numpy()[0,0]
                    expected=signal.lfilter(coeff[:3],np.r_[1.,coeff[3:]],x,axis=0)
                    actual=moving_filter(x,np.full((2,33),gain),np.zeros((2,33)),frequency,q,shelf,True)
                    error=float(np.max(abs(expected-actual)));maximum=max(maximum,error);count+=1;assert error<2e-7,error
                    direct=moving_filter(x,np.full((2,33),gain),np.zeros((2,33)),frequency,q,shelf,False)
                    assert np.max(abs(expected-direct))<2e-7
    # Extreme bounded frame modulation plus a long zero-input recovery.
    n=SR*8//256*256;source=rng.uniform(-1,1,size=(n,2));source[n//2:]=0;frames=n//256+1
    gains=rng.uniform(-30,30,size=(2,frames));offsets=rng.uniform(-2,2,size=(2,frames));stress=[]
    for shelf in [False,True]:
        y=moving_filter(source,gains,offsets,66.,1.6,shelf,True);assert np.isfinite(y).all()
        stress.append(dict(shelf=shelf,peak=float(np.max(abs(y))),last_second_peak=float(np.max(abs(y[-SR:])))))
    row=dict(stationary_cases=count,stationary_max_error=maximum,stress=stress,finite_test_not_stability_proof=True)
    (ROOT/'filter-realization-check.json').write_text(json.dumps(row,indent=2));print(json.dumps(row),flush=True)


def main(version):
    torch.set_num_threads(1);extension=importlib.import_module(MODULES[version]);extension.activate();model=extension.Model()
    checkpoint=torch.load(ROOT/f'surrogate-{version}-continuous-selected.pt',weights_only=True);model.load_state_dict(checkpoint['state_dict']);model.eval()
    rows={};paired={mode:[] for mode in ['original','parameter-linear','svf-parameter-linear']};target_pair=[];started=time.monotonic()
    for name in ['music-levels','history-plus','quality-tones','crest-validation-plus','crest-validation-minus']:
        d=extension.prepare_source(json.loads((ROOT/(name+'.json')).read_text())['job']['input']);target=read(name);row={}
        for mode in paired:
            output=extension.core.predict(model,d) if mode=='original' else render(model,d,mode=='svf-parameter-linear')
            assert np.isfinite(output).all();row[mode]=metrics(target[SR:],output[SR:])
            if name.startswith('crest-validation'):paired[mode].append(output)
            del output
        if name.startswith('crest-validation'):target_pair.append(target)
        rows[name]=row;print(name,{mode:round(r['residual_dbr'],5) for mode,r in row.items()},flush=True)
        del d,target
    cases=json.loads((ROOT/'crest-validation-cases.json').read_text());target_shape=paired_shape((target_pair[0]-target_pair[1])*.5,cases)
    plan=json.loads((ROOT/'selection-plan-v27.json').read_text());scores={}
    for mode,outputs in paired.items():
        shape=metrics(target_shape,paired_shape((outputs[0]-outputs[1])*.5,cases));components={name:rows[name][mode]['residual_dbr'] for name in ['music-levels','history-plus','quality-tones']};components['crest-validation-shape-change']=shape['residual_dbr']
        scores[mode]=dict(components=components,score=sum(plan['weights'][key]*value for key,value in components.items()))
    report=dict(version=version,step=checkpoint['step'],fitted=False,final_holdouts_used=False,seconds=time.monotonic()-started,rows=rows,scores=scores)
    (ROOT/f'filter-realization-{version}.json').write_text(json.dumps(report,indent=2));print(json.dumps(scores),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['check','audit']);parser.add_argument('--version',default='v27');args=parser.parse_args();check() if args.mode=='check' else main(args.version)
