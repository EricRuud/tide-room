"""Stationary diagnostics for the Airwindows listening experiment.

These measure non-harmonic energy, not physical authenticity or every alias.
All models see the same coherent input, rates, drive and measurement band.
ToTape9 uses native rate; Tide and Hybrid use 32x. See report for control settings.
"""
from pathlib import Path
import json
import sys
import numpy as np
import soundfile as sf

folder=Path(sys.argv[1]);rows=[];dynamic=[]
files=list(folder.glob('*.wav'))
if len(sys.argv)>2:
    files += [p for p in Path(sys.argv[2]).glob('*.wav') if not (p.name.startswith('hybrid-') or p.name.startswith('dynamic-hybrid-'))]
for path in sorted(files):
    x,rate=sf.read(path);x=x[:,0];spec=np.fft.rfft(x);power=abs(spec)**2
    if path.stem.startswith('dynamic-'):
        _,model,bin_,amplitude=path.stem.split('-');bin_=int(bin_);amplitude=int(amplitude)/100
        fundamental=2*abs(spec[bin_])/len(x)
        dynamic.append(dict(model=model,frequency=rate*bin_/len(x),input_amplitude=amplitude,
                            fundamental_gain_db=20*np.log10(fundamental/amplitude)))
        continue
    model,sr,bin_,drive=path.stem.split('-');bin_,sr,drive=map(int,(bin_,sr,drive));assert rate==sr
    mask=np.ones(len(spec),bool);mask[0]=False;mask[np.arange(1,4096//bin_+1)*bin_]=False
    band=np.arange(len(spec))<=int(.4*len(x))
    unwanted=float(power[mask&band].sum());wanted=float(power[~mask&band].sum())
    db=10*np.log10(max(unwanted,1e-30)/max(wanted,1e-30))
    rows.append(dict(file=path.name,model=model,rate=rate,frequency=rate*bin_/len(x),drive_db=drive,
                     unwanted_relative_db=db,peak=float(abs(x).max())))
assert len(rows)==90 and len(dynamic)==12
worst={name:max((r for r in rows if r['model']==name),key=lambda r:r['unwanted_relative_db']) for name in ['tide','airwindows','hybrid']}
comparisons=[]
for frequency in sorted(set(r['frequency'] for r in dynamic)):
    for amplitude in [.02,.2]:
        values={r['model']:r['fundamental_gain_db'] for r in dynamic if r['frequency']==frequency and r['input_amplitude']==amplitude}
        comparisons.append(dict(frequency=frequency,input_amplitude=amplitude,hybrid_vs_tide_db=values['hybrid']-values['tide']))
report=dict(criterion_db=-90,band='0 to 0.4 * sample rate',
            scope='Stationary single-tone non-harmonic energy, including numerical/filter error and reference noise; not a universal aliasing guarantee.',
            hybrid_passes=bool(worst['hybrid']['unwanted_relative_db']<=-90),worst=worst,dynamic_comparison=comparisons,dynamic_cases=dynamic,cases=rows)
(folder.parent/'spectral-report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k not in ['cases','dynamic_cases']},indent=2))
sys.exit(0 if report['hybrid_passes'] else 1)
