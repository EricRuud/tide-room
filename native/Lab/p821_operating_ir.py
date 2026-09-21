"""Incremental impulse responses around block-synchronous steady carriers.

The carrier has an integer number of cycles per 256-sample block. Probes land at
a raw-input zero crossing after 1.024 seconds of settling. Opposite probe signs
cancel the carrier and even-order perturbations. This reduces several detector
perturbations but does not prove all internal controls remain frozen.
"""
import argparse,json
import numpy as np
from scipy import signal
from p821_identify import ROOT,SR,save,submit
from p821_identification_analysis import read,linear_kernel,metrics,db

BLOCK=256


def capture():
    cases=[];carriers=[];probes=[];length=384*BLOCK
    for frequency in [187.5,6000]:
        for peak in [.2,.7]:
            for channel in [0,1]:
                c=np.zeros((length,2));p=np.zeros_like(c)
                n=288*BLOCK;t=np.arange(n)/SR
                tone=peak*np.sin(2*np.pi*frequency*t)
                tone[:BLOCK]*=np.linspace(0,1,BLOCK)
                tone[-BLOCK:]*=np.linspace(1,0,BLOCK)
                c[:n]=tone[:,None]
                pos=192*BLOCK;p[pos,channel]=.001
                offset=len(cases)*length
                cases.append(dict(frequency=frequency,peak=peak,channel=channel,start=offset,probe=offset+pos,end=offset+length))
                carriers.append(c);probes.append(p)
    c=np.concatenate(carriers);p=np.concatenate(probes)
    (ROOT/'operating-ir-cases.json').write_text(json.dumps(cases,indent=2))
    for name,x in [('carrier',c),('plus',c+p),('minus',c-p)]:
        submit('operating-ir-'+name,save('operating-ir-'+name,x))


def analyze():
    plus,minus,carrier=[read('operating-ir-'+name) for name in ['plus','minus','carrier']]
    marginal=(plus-minus)/.002
    h=linear_kernel();w=h[:,:,0]/(h[0,0,0]+h[0,1,0]);inv=np.linalg.inv(w)
    marginal=marginal@inv.T;quiet=np.einsum('ij,jkl->ikl',inv,h)
    rows=[];impulses=[]
    for case in json.loads((ROOT/'operating-ir-cases.json').read_text()):
        a=case['probe'];b=a+16384;channel=case['channel']
        response=marginal[a:b,channel];q=quiet[channel,channel,:len(response)]
        fft=np.fft.rfft(response);qfft=np.fft.rfft(q);hz=np.fft.rfftfreq(len(response),1/SR)
        row=case|{'first_sample':float(response[0]),'quiet_first_sample':float(q[0]),'cross_channel_relative_db':db(np.linalg.norm(marginal[a:b,1-channel])/np.linalg.norm(response)),
                  'ratio_at_hz':{str(f):{'gain_db':db(abs(fft[k]/qfft[k])),'phase_degrees':float(np.angle(fft[k]/qfft[k])*180/np.pi)} for f in [50,100,180,500,1000,2700,8000,16000] for k in [int(np.argmin(abs(hz-f))) ]},
                  'central_even_error_peak':float(np.max(abs((plus+minus)[a:b]*.5-carrier[a:b])))}
        rows.append(row);impulses.append(response)
    np.savez_compressed(ROOT/'operating-ir.npz',responses=np.array(impulses),quiet=quiet)
    (ROOT/'operating-ir-analysis.json').write_text(json.dumps(rows,indent=2))
    for row in rows:print(row['frequency'],row['peak'],row['channel'],row['first_sample'],row['ratio_at_hz'],flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['capture','analyze'])
    capture() if parser.parse_args().mode=='capture' else analyze()
