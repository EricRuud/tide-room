"""Resolve the narrow frequency region where driven waveforms flatten.

All comparisons use the same left input. Harmonic fits use complete steady
waveforms, while total residual after fitting also includes modulation/noise.
"""
import argparse,json
import numpy as np
from p821_identify import ROOT,SR,save,submit
from p821_identification_analysis import read,linear_kernel,db


def capture():
    cases=[];parts=[np.zeros((384*256,2))];offset=len(parts[0])
    for f in [100,137,160,180,200,225,250,275,293,350,500,1000]:
        for mode,pan in [('mono',[1,1]),('pan',[1,.6]),('left',[1,0])]:
            n=192*256;t=np.arange(n)/SR;env=np.ones(n);env[:64]=np.linspace(0,1,64);env[-64:]=np.linspace(1,0,64)
            x=.7*env*np.sin(2*np.pi*f*t)
            parts.extend([x[:,None]*pan,np.zeros((96*256,2))]);cases.append(dict(start=offset,end=offset+n,frequency=f,peak=.7,mode=mode));offset+=288*256
    (ROOT/'saturation-map-cases.json').write_text(json.dumps(cases,indent=2))
    submit('saturation-map',save('saturation-map',np.concatenate(parts)))


def analyze():
    h=linear_kernel();w=h[:,:,0]/(h[0,0,0]+h[0,1,0]);y=read('saturation-map')@np.linalg.inv(w).T
    rows=[]
    for c in json.loads((ROOT/'saturation-map-cases.json').read_text()):
        a=c['start']+int(.5*SR);b=c['end']-128;t=np.arange(a-c['start'],b-c['start'])/SR
        order=np.arange(1,10);phase=2*np.pi*c['frequency']*t[:,None]*order
        A=np.concatenate([np.sin(phase),np.cos(phase),np.ones((len(t),1))],axis=1)
        coeff=np.linalg.lstsq(A,y[a:b,0],rcond=None)[0];amps=np.hypot(coeff[:9],coeff[9:18]);fit=A@coeff
        row=c|{'harmonics_dbr':[db(v/amps[0]) for v in amps],'thd_dbr':db(np.linalg.norm(amps[1:])/amps[0]),'unfitted_dbr':db(np.linalg.norm(y[a:b,0]-fit)/np.linalg.norm(y[a:b,0])),'output_peak':float(np.max(abs(y[a:b,0]))),'fundamental_gain_db':db(amps[0]/c['peak'])}
        rows.append(row);print(c['frequency'],c['mode'],'THD',round(row['thd_dbr'],2),'H3',round(row['harmonics_dbr'][2],2),'peak',round(row['output_peak'],4),flush=True)
    (ROOT/'saturation-map-analysis.json').write_text(json.dumps(rows,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['capture','analyze'])
    capture() if parser.parse_args().mode=='capture' else analyze()
