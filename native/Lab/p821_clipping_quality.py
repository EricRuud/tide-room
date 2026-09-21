"""Exercise clipping with bass plus treble and unusually high single tones."""
import argparse,json
import numpy as np
import torch
from p821_identify import ROOT,SR,save,submit
from p821_identification_analysis import read,metrics,db


def capture():
    cases=[];parts=[np.zeros((384*256,2))];offset=len(parts[0])
    for mode,pan in [('mono',[1,1]),('pan',[1,.6]),('left',[1,0])]:
        for low,high in [(.7,.03),(.7,.1),(0,1.4)]:
            n=288*256;t=np.arange(n)/SR;env=np.ones(n);env[:128]=np.linspace(0,1,128);env[-128:]=np.linspace(1,0,128)
            x=env*(low*np.sin(2*np.pi*160*t)+high*np.sin(2*np.pi*17000*t))
            parts.extend([x[:,None]*pan,np.zeros((96*256,2))])
            cases.append(dict(start=offset,end=offset+n,low_peak=low,high_peak=high,mode=mode));offset+=384*256
    (ROOT/'clipping-quality-cases.json').write_text(json.dumps(cases,indent=2))
    submit('clipping-quality',save('clipping-quality',np.concatenate(parts)))


def audit():
    import p821_adaptive_clip_model as extension
    extension.activate();core=extension.core;torch.set_num_threads(2)
    checkpoint=torch.load(ROOT/'surrogate-v13-best.pt',weights_only=True);model=core.Model();model.load_state_dict(checkpoint['state_dict']);model.eval()
    job=json.loads((ROOT/'clipping-quality.json').read_text())['job'];d=core.prepare_source(job['input']);yp=core.predict(model,d);yt=read('clipping-quality')
    rows=[]
    for c in json.loads((ROOT/'clipping-quality-cases.json').read_text()):
        # 1.024 seconds is coherent with 17 kHz and the three-buffer RMS cycle.
        # A Hann window also suppresses leakage from remaining slow settling.
        a=c['start']+72*256;b=a+192*256
        row=c|metrics(yt[a:b],yp[a:b])
        # Every genuine harmonic or intermodulation product of 160 and 17000 Hz
        # is on a 40 Hz lattice. Folded products also land on that lattice, so
        # a lattice-only metric would NOT isolate aliases for the mixed tests.
        if c['low_peak']==0:
            for label,y in [('P821',yt),('model',yp)]:
                spectrum=np.fft.rfft(y[a:b,0]*np.hanning(b-a));index=lambda f:round(f*(b-a)/SR);fund=abs(spectrum[index(17000)])
                row[label+'_fold_3000_dbr']=db(abs(spectrum[index(3000)])/fund)
                row[label+'_fold_11000_dbr']=db(abs(spectrum[index(11000)])/fund)
        rows.append(row);print(row,flush=True)
    (ROOT/'clipping-quality-v13-audit.json').write_text(json.dumps(dict(checkpoint_step=checkpoint['step'],cases=rows),indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['capture','audit']);capture() if parser.parse_args().mode=='capture' else audit()
