"""Summarize observed P821 behavior, without fitting or exporting an emulation."""
import json
import os
from pathlib import Path

os.environ.setdefault('MPLCONFIGDIR','/private/tmp/tide-mpl')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
import soundfile as sf
from scipy import signal

ROOT=Path(__file__).resolve().parents[2]/'artifacts/p821-analysis-003'


def db(x): return 20*np.log10(np.maximum(np.abs(x),1e-15))


def tones(name):
    report=json.loads((ROOT/(name+'.json')).read_text())
    assert report['ok']
    x,sr=sf.read(report['job']['input'],always_2d=True)
    y,ysr=sf.read(report['job']['output'],always_2d=True)
    assert sr==ysr and x.shape==y.shape
    records=json.loads(Path(report['job']['input']).with_suffix('.json').read_text())
    rows=[]
    for rec in records:
        duration=rec.get('analysis_seconds',.5)
        a,b=rec['end']-int(sr*duration),rec['end']
        f=rec['frequency']; t=np.arange(a-rec['start'],b-rec['start'])/sr
        # Complex amplitude: magnitude is peak amplitude, phase references input.
        exp=np.exp(-2j*np.pi*f*t)
        inp=2*exp@x[a:b]/len(t); out=2*exp@y[a:b]/len(t)
        mid=(out[0]+out[1])/2;side=(out[0]-out[1])/2
        spec=2*np.fft.rfft(y[a:b],axis=0)/len(t)
        fund=int(round(f*duration));harmbins=np.arange(fund,spec.shape[0],fund)
        energy=np.abs(spec)**2
        harmonic=float(np.sum(energy[harmbins[1:]])/max(np.sum(energy[fund]),1e-30))
        bins=np.fft.rfftfreq(len(t),1/sr)
        mask=(bins>=12)&(bins<=20000);mask[harmbins]=False
        residual=np.sum(energy[mask])/max(np.sum(energy[fund]),1e-30)
        top=sorted(np.where(mask)[0],key=lambda i:energy[i].sum(),reverse=True)[:5]
        row=rec|{'output_amplitude':np.abs(out).tolist(),'output_phase_degrees':np.angle(out,deg=True).tolist(),
                  'input_amplitude':np.abs(inp).tolist(),'mid_amplitude':float(abs(mid)),
                  'side_amplitude':float(abs(side)),'side_energy_fraction':float(abs(side)**2/max(abs(side)**2+abs(mid)**2,1e-30)),
                  'lr_phase_degrees':float(np.angle(out[1]/out[0],deg=True)) if abs(out[0])>1e-20 else 0,
                  'gain_db':float(db(np.linalg.norm(out)/max(np.linalg.norm(inp),1e-30))),
                  'thd_percent':float(100*np.sqrt(harmonic)),
                  'nonharmonic_dbr':float(10*np.log10(max(residual,1e-30))),
                  'spurs':[{'frequency':float(bins[i]),'level_dbr':float(10*np.log10(max(energy[i].sum()/max(energy[fund].sum(),1e-30),1e-30)))} for i in top]}
        rows.append(row)
    return rows


def main():
    report={'stereo':{},'levels':{},'spectral':{},'music':{},'validation':{}}
    for group,pattern in [('stereo','stereo-*.json'),('levels','levels-*.json'),('spectral','spectral-*.json'),('spectral','alias-verified-*.json'),('levels','bias-*.json')]:
        for path in sorted(ROOT.glob(pattern)):
            try:report[group][path.stem]=tones(path.stem)
            except (KeyError,FileNotFoundError):continue
    for path in sorted(ROOT.glob('music-*.json')):
        d=json.loads(path.read_text());x,sr=sf.read(d['job']['input']);y,_=sf.read(d['job']['output'])
        bands={}
        for lo,hi in [(20,120),(120,500),(500,2000),(2000,8000),(8000,20000)]:
            sos=signal.butter(4,[lo,hi],fs=sr,btype='bandpass',output='sos')
            v=signal.sosfilt(sos,y,axis=0)[sr:]
            mid=(v[:,0]+v[:,1])*.5;side=(v[:,0]-v[:,1])*.5
            bands[str(lo)+'-'+str(hi)]={'side_fraction':float(np.sum(side**2)/max(np.sum(mid**2+side**2),1e-30)),
                                      'lr_correlation':float(np.corrcoef(v.T)[0,1])}
        report['music'][path.stem]={'bands':bands,'rms':float(np.sqrt(np.mean(y*y))),
                                  'input_difference_rms':float(np.sqrt(np.mean((y-x)**2))),'cpu_percent':d['cpuPercent']}
    original,sr=sf.read(ROOT/'input-room.wav');bands={}
    for lo,hi in [(20,120),(120,500),(500,2000),(2000,8000),(8000,20000)]:
        v=signal.sosfilt(signal.butter(4,[lo,hi],fs=sr,btype='bandpass',output='sos'),original,axis=0)[sr:]
        mid=(v[:,0]+v[:,1])*.5;side=(v[:,0]-v[:,1])*.5
        bands[str(lo)+'-'+str(hi)]={'side_fraction':float(np.sum(side**2)/max(np.sum(mid**2+side**2),1e-30)),
                                   'lr_correlation':float(np.corrcoef(v.T)[0,1])}
    report['music']['original']={'bands':bands}
    for name in ['demo-continuity','demo-continuity-verified']:
        p=ROOT/(name+'.wav')
        if p.exists():
            y,sr=sf.read(p);r=np.sqrt(np.mean(y[:len(y)//4800*4800].reshape(-1,4800,2)**2,axis=(1,2)))
            report['validation'][name]={'zero_100ms_windows':int(np.sum(r<1e-6)),'rms_min_after_2s':float(min(r[20:])),
                                      'rms_max_after_2s':float(max(r[20:]))}
    (ROOT/'analysis.json').write_text(json.dumps(report,indent=2))
    fig,axs=plt.subplots(2,2,figsize=(12,8),layout='constrained')
    for key,rows in report['stereo'].items():
        if 'tape-' not in key or 'repeat' in key:continue
        mono=[r for r in rows if r['mode']=='mono']; pan=[r for r in rows if r['mode']=='pan']
        f=[r['frequency'] for r in mono]
        focus=int(key.split('-f')[1][0]);center=int(key[-1])
        label=['Off','Half','Full'][focus]+(' + CENTER' if center else '')
        axs[0,0].semilogx(f,[r['gain_db'] for r in mono],label=label)
        axs[0,1].semilogx(f,[r['side_energy_fraction']*100 for r in mono],label=label)
        axs[1,0].semilogx(f,[r['side_energy_fraction']*100 for r in pan],label=label)
        axs[1,1].semilogx(f,[r['lr_phase_degrees'] for r in pan],label=label)
    for ax,title,ylabel in zip(axs.flat,['Centered tone: gain','Centered tone: side energy','Partially panned tone: side energy','Partially panned tone: L/R phase'],['dB','%','%','degrees']):
        ax.set(title=title,xlabel='Frequency (Hz)',ylabel=ylabel);ax.grid(alpha=.2);ax.legend(fontsize=8)
    fig.suptitle('P821 stereo behavior | 456 / 15 ips / −36 dBFS peak | noise and motion off')
    axs[0,1].set_ylim(0,.1)
    axs[1,0].axhline(10,color='black',linestyle=':',label='Input (L=1, R=0.5)');axs[1,0].legend(fontsize=8)
    axs[1,1].set_ylim(-1,1)
    fig.savefig(ROOT/'stereo-analysis.png',dpi=160);plt.close(fig)
    print(json.dumps({'completed_groups':{k:len(v) for k,v in report.items()},'validation':report['validation']},indent=2))
    extra={}
    for name in ['noise-off','noise-on']:
        x,sr=sf.read(ROOT/(name+'.wav'));x=x[sr:]
        extra[name]={'rms_dbfs_floor_minus300':float(db(max(np.sqrt(np.mean(x*x)),1e-15))),
                     'lr_correlation':float(np.corrcoef(x.T)[0,1])}
    for name in ['motion-off','motion-default','motion-wow','motion-flutter']:
        x,sr=sf.read(ROOT/(name+'.wav'));phase=np.unwrap(np.angle(signal.hilbert(x,axis=0)),axis=0)
        hz=np.diff(phase,axis=0)*sr/(2*np.pi)
        hz=signal.sosfiltfilt(signal.butter(3,100,fs=sr,output='sos'),hz,axis=0)[sr:-sr:48]
        cents=1200*np.log2(np.maximum(hz,1)/3000)
        extra[name]={'pitch_cents_p01_p99':np.percentile(cents,[1,99],axis=0).tolist(),
                     'pitch_cents_rms':np.sqrt(np.mean(cents*cents,axis=0)).tolist(),
                     'lr_difference_dbr':float(db(np.sqrt(np.mean((x[:,0]-x[:,1])**2))/max(np.sqrt(np.mean(x[:,0]**2)),1e-20)))}
    for name in ['input-bursts','bursts-f0','bursts-f1']:
        x,sr=sf.read(ROOT/(name+'.wav'));x=x[:,0];seg=x[sr:sr+int(.2*sr)]**2;t=np.arange(len(seg))/sr
        extra[name]={'first_burst_energy_centroid_ms':float(np.sum(t*seg)/sum(seg)*1000),
                     'energy_in_first_10ms_fraction':float(sum(seg[:int(.01*sr)])/sum(seg)),
                     'energy_after_20ms_fraction':float(sum(seg[int(.02*sr):])/sum(seg))}
    x,sr=sf.read(ROOT/'stereo-tape-f2-c0.wav');y,_=sf.read(ROOT/'stereo-tape-repeat.wav')
    extra['repeat']={'rms_difference':float(np.sqrt(np.mean((x-y)**2))),
                     'relative_db':float(db(np.sqrt(np.mean((x-y)**2))/np.sqrt(np.mean(x*x))))}
    (ROOT/'motion-noise-transients.json').write_text(json.dumps(extra,indent=2))


if __name__=='__main__':main()
