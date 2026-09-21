"""Target-informed residual projections: diagnose errors, do not report a null.

Per-block fitted gains/EQ have direct access to target audio. Their purpose is
to distinguish gain timing, channel coupling, and spectral residual components.
"""
import json
import numpy as np
import torch
from scipy import signal
from p821_identification_analysis import ROOT,SR,read,metrics
import p821_memory_control_model as extension


def main():
    extension.activate();core=extension.core;torch.set_num_threads(2)
    checkpoint=torch.load(ROOT/'surrogate-v18-step-10000.pt',weights_only=True);m=core.Model();m.load_state_dict(checkpoint['state_dict']);m.eval()
    d=core.prepare_source(json.loads((ROOT/'music-levels.json').read_text())['job']['input']);pred=core.predict(m,d);target=read('music-levels');n=len(pred)//256*256;inv=np.linalg.inv(d['w']).T
    pred=pred[:n]@inv;target=target[:n]@inv;bb=pred.reshape(-1,256,2);yy=target.reshape(-1,256,2);t=np.linspace(-.5,.5,256)[None,:,None]
    variants={'unaltered':pred}
    constant=np.sum(bb*yy,axis=(1,2))/np.maximum(np.sum(bb*bb,axis=(1,2)),1e-30);variants['shared_constant_gain']=(bb*constant[:,None,None]).reshape(-1,2)
    for linked in [False,True]:
        A=np.stack([bb,bb*t],axis=-1)
        if linked:
            A=A.reshape(-1,512,2);Y=yy.reshape(-1,512)
        else:A=A.transpose(0,2,1,3);Y=yy.transpose(0,2,1)
        gram=np.einsum('...ni,...nj->...ij',A,A);rhs=np.einsum('...ni,...n->...i',A,Y);scale=np.trace(gram,axis1=-2,axis2=-1)*1e-12+1e-25
        coef=np.linalg.solve(gram+scale[...,None,None]*np.eye(2),rhs[...,None])[...,0];out=np.einsum('...ni,...i->...n',A,coef)
        if not linked:out=out.transpose(0,2,1)
        variants['shared_ramping_gain' if linked else 'separate_ramping_gain']=out.reshape(-1,2)
    pool=[pred]
    for f in [100,300,1000,3000,8000,16000]:
        pool.append(signal.sosfilt(signal.butter(2,f,fs=SR,output='sos'),pred,axis=0))
        b,a=signal.iirpeak(f,1,fs=SR);pool.append(signal.lfilter(b,a,pred,axis=0))
    A=np.stack(pool,axis=2).reshape(-1,256,2,len(pool)).transpose(0,2,1,3);Y=yy.transpose(0,2,1)
    gram=np.einsum('bcni,bcnj->bcij',A,A);rhs=np.einsum('bcni,bcn->bci',A,Y);scale=np.trace(gram,axis1=-2,axis2=-1)*1e-10+1e-25
    coef=np.linalg.solve(gram+scale[:,:,None,None]*np.eye(len(pool)),rhs[...,None])[...,0]
    variants['separate_filter_basis']=np.einsum('bcni,bci->bcn',A,coef).transpose(0,2,1).reshape(-1,2)
    report=dict(warning='Uses target directly; diagnostic projections, not emulator or independent scores',checkpoint_step=checkpoint['step'],passages=[])
    for c in json.loads((ROOT/'input-music-levels.json').read_text()):
        a,b=c['start'],min(c['end'],n);r=c|dict(projections={name:metrics(target[a:b]@d['w'].T,y[a:b]@d['w'].T) for name,y in variants.items()});report['passages'].append(r)
        print(c['gain_db'],{k:round(v['residual_dbr'],2) for k,v in r['projections'].items()},flush=True)
    (ROOT/'residual-projection-v18.json').write_text(json.dumps(report,indent=2))


if __name__=='__main__':main()
