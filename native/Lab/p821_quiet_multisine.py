"""Quiet best-linear-approximation experiment with periodic random multisines.

Inspired by Schoukens/Vaes/Pintelon's nonlinear identification guidelines:
discard settling periods; average periods and independent phase realizations;
retain uncertainty and unexcited-bin energy. It is not a universal linear model
of the plugin. Flat/pink spectra test dependence on excitation statistics.
"""
import argparse,json
import numpy as np
from p821_identify import ROOT,SR,save,submit
from p821_identification_analysis import read,linear_kernel,db
from p821_slow_detector import predict_curve

N=65536


def capture():
    rng=np.random.default_rng(95821);parts=[np.zeros((384*256,2))];cases=[];offset=len(parts[0]);hz=np.fft.rfftfreq(N,1/SR)
    active=(np.arange(len(hz))%2==1)&(hz>=10)&(hz<=22000)
    for shape in ['flat','pink']:
        phases=[rng.uniform(-np.pi,np.pi,len(hz)) for _ in range(4)]
        for level in [.001,.004,.015,.04,.08]:
            for realization,phase in enumerate(phases):
                spectrum=active*np.exp(1j*phase)
                if shape=='pink':spectrum/=np.sqrt(np.maximum(hz,25))
                period=np.fft.irfft(spectrum,N);period*=level/np.sqrt(np.mean(period**2));x=np.tile(period,4)[:,None]*[1,1];parts.append(x)
                cases.append(dict(start=offset,end=offset+len(x),shape=shape,rms=level,realization=realization,peak=float(np.max(abs(x)))));offset+=len(x)
    (ROOT/'quiet-multisine-cases.json').write_text(json.dumps(cases,indent=2));submit('quiet-multisine',save('quiet-multisine',np.concatenate(parts)))


def analyze():
    x=read('input-quiet-multisine');y=read('quiet-multisine');cases=json.loads((ROOT/'quiet-multisine-cases.json').read_text());hz=np.fft.rfftfreq(N,1/SR);active=(np.arange(len(hz))%2==1)&(hz>=10)&(hz<=22000)
    rms=np.sqrt(np.mean(x.reshape(-1,256,2)**2,axis=1));levels=np.mean(np.maximum(20*np.log10(np.maximum(rms,1e-10)),-90),axis=1);p=np.array(list(json.loads((ROOT/'slow-detector-fit.json').read_text())[1]['parameters'].values()));g=predict_curve(levels,p,'after');old=np.concatenate([[p[4]],g[:-1]]);fraction=np.arange(1,257)[None,:]/256;gain=10**((old[:,None]*(1-fraction)+g[:,None]*fraction).reshape(-1)/20);corrected=y.mean(axis=1)/gain
    h=linear_kernel();H=np.fft.rfft(h[0,0]+h[0,1],N);measurements=[];groups={};rows=[]
    for c in cases:
        a,b=c['start'],c['end'];xx=x[a:b,0].reshape(4,N)[1:];yy=corrected[a:b].reshape(4,N)[1:];X=np.fft.rfft(xx,axis=1);Y=np.fft.rfft(yy,axis=1);frf=Y[:,active]/X[:,active];mean=frf.mean(axis=0);groups.setdefault((c['shape'],c['rms']),[]).append(mean);measurements.append(mean)
        noise=np.mean(abs(frf-mean)**2,axis=0);rows.append(c|dict(period_repeat_deviation_dbr=db(np.sqrt(noise.sum()/np.sum(abs(mean)**2))),even_unexcited_energy_dbr=db(np.linalg.norm(Y[:,~active])/np.linalg.norm(Y[:,active]))))
    averaged=[];summary=[]
    for key,values in groups.items():
        values=np.array(values);mean=values.mean(axis=0);variance=np.mean(abs(values-mean)**2,axis=0);averaged.append(mean)
        ratio=mean/H[active];row=dict(shape=key[0],rms=key[1],baseline_relative_error_dbr=db(np.linalg.norm(mean-H[active])/np.linalg.norm(mean)),realization_deviation_dbr=db(np.sqrt(variance.sum()/np.sum(abs(mean)**2))),response={})
        for f in [25,50,63,100,180,300,1000,2700,8000,16000]:
            k=np.argmin(abs(hz[active]-f));row['response'][str(f)]=dict(gain_db=db(abs(ratio[k])),phase_degrees=float(np.degrees(np.angle(ratio[k]))),relative_std_db=db(np.sqrt(variance[k])/abs(mean[k])))
        summary.append(row);print(key,round(row['baseline_relative_error_dbr'],2),'phase-realization spread',round(row['realization_deviation_dbr'],2),flush=True)
    np.savez_compressed(ROOT/'quiet-multisine-analysis.npz',frequency=hz[active],measurements=np.array(measurements),averaged=np.array(averaged),original_response=H[active])
    (ROOT/'quiet-multisine-analysis.json').write_text(json.dumps(dict(period_length=N,discarded_periods=1,averaged_periods=3,realizations=4,source_only_slow_gain_correction=True,cases=rows,summary=summary),indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['capture','analyze']);globals()[parser.parse_args().mode]()
