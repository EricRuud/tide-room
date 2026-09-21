"""Independent post-burst cases, excluded from all model fitting.

New frequencies, levels and durations test whether recovery supervision learns
more than the training grid. These are development diagnostics; the three final
musical holdouts remain untouched.
"""
import argparse,importlib,json
import numpy as np
import torch
from p821_identify import ROOT,SR,save,submit
from p821_identification_analysis import read,metrics
from p821_select_continuous import MODULES


def capture():
    frequencies=[93.75,281.25,468.75,1125,3000,7500];conditions=[(f,l,d) for f in frequencies for l in [.8,1.2,1.6] for d in [9,36,144]]
    np.random.default_rng(927821).shuffle(conditions)
    carriers=[np.zeros((384*256,2))];pilots=[np.zeros_like(carriers[0])];cases=[];offset=len(carriers[0])
    for f,level,duration in conditions:
        n=448*256;t=np.arange(n)/SR;a=64*256;b=a+duration*256;c=np.zeros((n,2));p=np.zeros_like(c)
        c[a:b,0]=level*np.sin(2*np.pi*f*np.arange(b-a)/SR);p[:,0]=sum(np.sin(2*np.pi*q*t) for q in [9000,15000,21000])*.0005
        carriers.append(c);pilots.append(p);cases.append(dict(start=offset,end=offset+n,conditioner_start=offset+a,conditioner_end=offset+b,frequency=f,level=level,duration_seconds=duration*256/SR));offset+=n
    (ROOT/'release-generalization-cases.json').write_text(json.dumps(cases,indent=2));c=np.concatenate(carriers);p=np.concatenate(pilots)
    for sign,label in [(1,'plus'),(-1,'minus')]:submit('release-generalization-'+label,save('release-generalization-'+label,c+sign*p))
    print('Independent recovery capture completed',len(cases),'cases',flush=True)


def audit(version):
    extension=importlib.import_module(MODULES[version]);extension.activate();core=extension.core;torch.set_num_threads(2)
    checkpoint=torch.load(ROOT/f'surrogate-{version}-continuous-selected.pt',weights_only=True);m=core.Model();m.load_state_dict(checkpoint['state_dict']);m.eval();predictions=[];targets=[]
    for label in ['plus','minus']:
        name='release-generalization-'+label;d=core.prepare_source(json.loads((ROOT/(name+'.json')).read_text())['job']['input']);predictions.append(core.predict(m,d));targets.append(read(name))
    target=(targets[0]-targets[1])*.5@np.linalg.inv(d['w']).T;prediction=(predictions[0]-predictions[1])*.5@np.linalg.inv(d['w']).T
    cases=json.loads((ROOT/'release-generalization-cases.json').read_text());osc=np.exp(-2j*np.pi*np.arange(256)[:,None]*np.array([9000,15000,21000])[None,:]/SR)
    actual=target[:,0].reshape(-1,256)@osc;predicted=prediction[:,0].reshape(-1,256)@osc;rows=[];yt=[];yp=[]
    for c in cases:
        a=c['conditioner_end'];b=a+96*256;yt.append(target[a+24:b,0]);yp.append(prediction[a+24:b,0]);row=c|metrics(target[a+24:b,0],prediction[a+24:b,0])
        first=a//256;normal=np.mean(actual[c['conditioner_start']//256-16:c['conditioner_start']//256-4],axis=0)
        ref=actual[first:first+96]/normal;out=predicted[first:first+96]/normal;gain_error=20*np.log10(np.maximum(abs(out),1e-12)/np.maximum(abs(ref),1e-12));phase_error=np.degrees(np.angle(out*np.conj(ref)))
        row['gain_error_rmse_db']=np.sqrt(np.mean(gain_error**2,axis=0)).tolist();row['phase_error_rmse_degrees']=np.sqrt(np.mean(phase_error**2,axis=0)).tolist();row['reference_gain_db']=(20*np.log10(np.maximum(abs(ref),1e-12))).tolist();row['predicted_gain_db']=(20*np.log10(np.maximum(abs(out),1e-12))).tolist();rows.append(row)
    report=dict(version=version,step=checkpoint['step'],unfit_recovery_cases=True,final_musical_holdouts_used=False,overall=metrics(np.concatenate(yt),np.concatenate(yp)),rows=rows)
    (ROOT/f'release-generalization-{version}.json').write_text(json.dumps(report,indent=2));print(version,'recovery waveform residual',report['overall']['residual_dbr'],flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['capture','audit']);parser.add_argument('--version',default='v20',choices=list(MODULES));args=parser.parse_args()
    capture() if args.mode=='capture' else audit(args.version)
