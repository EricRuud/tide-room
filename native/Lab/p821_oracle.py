"""Diagnostic projection using target audio; not an emulation or validation score."""
import json
import numpy as np
import soundfile as sf
from scipy import signal
from p821_identification_analysis import ROOT, SR, linear_kernel, linear_render, metrics


def main():
    h = linear_kernel()
    w = h[:,:,0]/(h[0,0,0]+h[0,1,0])
    inv = np.linalg.inv(w)
    all_rows = {}
    for name in ['training-noise', 'music-levels', 'envelope-plus', 'training-patterns']:
        report = json.loads((ROOT/(name+'.json')).read_text())
        x, sr = sf.read(report['job']['input'], always_2d=True)
        y, _ = sf.read(report['job']['output'], always_2d=True)
        base = linear_render(x, h) @ inv.T
        y = y @ inv.T
        n = len(x)//256*256
        bb, yy = base[:n].reshape(-1,256,2), y[:n].reshape(-1,256,2)
        g = np.sum(bb*yy,axis=1)/np.maximum(np.sum(bb*bb,axis=1),1e-20)
        gp = (bb*g[:,None,:]).reshape(-1,2)
        row = {'gain_only': metrics(y[SR:n],gp[SR:n])}
        pool = [base]
        for f in [100, 300, 1000, 3000, 8000, 16000]:
            pool.append(signal.sosfilt(signal.butter(2,f,fs=SR,output='sos'),base,axis=0))
            b,a=signal.iirpeak(f,1,fs=SR)
            pool.append(signal.lfilter(b,a,base,axis=0))
        A=np.stack(pool,axis=2)[:n].reshape(-1,256,2,len(pool)).transpose(0,2,1,3)
        B=yy.transpose(0,2,1)
        gram=np.einsum('bcni,bcnj->bcij',A,A)
        rhs=np.einsum('bcni,bcn->bci',A,B)
        regularizer=np.trace(gram,axis1=-2,axis2=-1)*1e-8+1e-20
        gram+=regularizer[:,:,None,None]*np.eye(len(pool))[None,None]
        coef=np.linalg.solve(gram,rhs[...,None])[...,0]
        pred=np.einsum('bcni,bci->bcn',A,coef).transpose(0,2,1).reshape(-1,2)
        row['filter_basis']=metrics(y[SR:n],pred[SR:n])
        all_rows[name]=row
        np.savez_compressed(ROOT/(name+'-oracle.npz'),gain=g,coefficients=coef)
    (ROOT/'oracle-analysis.json').write_text(json.dumps(all_rows,indent=2))
    print(json.dumps(all_rows,indent=2))


if __name__=='__main__':main()
