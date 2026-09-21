"""Steady clipping with an integer number of cycles in every 256-sample block.

This removes input-RMS/peak beating against the callback as an explanation for
the measured third-harmonic phase. Harmonic phase is diagnostic, not a topology
proof: multiple nonlinear stages and other memory mechanisms remain possible.
"""
import argparse,json
import numpy as np
from p821_identify import ROOT,SR,save,submit
from p821_identification_analysis import read,linear_kernel,db


def capture():
    parts=[np.zeros((384*256,2))];cases=[];offset=len(parts[0])
    for f in [187.5,375,1500]:
        for level,mode,pan in [(.7,'mono',[1,1]),(.7,'left',[1,0]),(1.4,'left',[1,0])]:
            n=288*256;t=np.arange(n)/SR;x=level*np.sin(2*np.pi*f*t)
            parts.extend([x[:,None]*pan,np.zeros((96*256,2))]);cases.append(dict(start=offset,end=offset+n,frequency=f,peak=level,mode=mode));offset+=384*256
    (ROOT/'coherent-clip-cases.json').write_text(json.dumps(cases,indent=2));submit('coherent-clip',save('coherent-clip',np.concatenate(parts)))


def analyze():
    h=linear_kernel();w=h[:,:,0]/(h[0,0,0]+h[0,1,0]);y=read('coherent-clip')@np.linalg.inv(w).T;rows=[]
    for c in json.loads((ROOT/'coherent-clip-cases.json').read_text()):
        a=c['start']+144*256;b=c['end']-24*256;t=np.arange(a,b)/SR;orders=np.arange(1,10);phase=2*np.pi*c['frequency']*t[:,None]*orders
        A=np.concatenate([np.sin(phase),np.cos(phase),np.ones((len(t),1))],axis=1);co=np.linalg.lstsq(A,y[a:b,0],rcond=None)[0];z=co[:9]+1j*co[9:18]
        row=c|dict(h3_dbr=db(abs(z[2]/z[0])),excess_h3_phase_degrees=float(np.degrees(np.angle(z[2]/z[0]**3))),peak_output=float(np.max(abs(y[a:b,0]))),fundamental_gain_db=db(abs(z[0])/c['peak']))
        rows.append(row);print(row,flush=True)
    (ROOT/'coherent-clip-analysis.json').write_text(json.dumps(rows,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['capture','analyze']);capture() if parser.parse_args().mode=='capture' else analyze()
