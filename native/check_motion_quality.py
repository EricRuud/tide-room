"""Verify unwanted control-update sidebands using TideQualityAudit's actual DSP renders.
Usage: python3 native/check_motion_quality.py BEFORE_DIR AFTER_DIR OUTPUT_JSON
Both inputs: TideQualityAudit --motion-tone ABSOLUTE_DIR (600 Hz, 0.4 Hz motion).
"""
import json,sys
from pathlib import Path
import numpy as np
import soundfile as sf

def measure(path):
    x,sr=sf.read(path,always_2d=True)
    assert sr==48000 and x.shape==(960000,2), 'Expected 20 s steady stereo at 48 kHz'
    assert np.isfinite(x).all() and np.max(np.abs(x))<1, 'Invalid or clipped output'
    f=np.fft.rfftfreq(len(x),1/sr)
    power=(np.abs(np.fft.rfft(x,axis=0))**2).sum(axis=1)
    wanted=np.abs(f-600)<20
    images=np.zeros(len(f),bool)
    for k in [-2,-1,1,2]:images|=np.abs(f-(600+k*sr/512))<20
    return {'control_images_dBr':float(10*np.log10(power[images].sum()/power[wanted].sum())),
            'rms_dBFS':float(20*np.log10(np.sqrt(np.mean(x*x))))}

before,after,output=map(Path,sys.argv[1:]);result={}
for name,limit in [('motion-tone-actual.wav',-90),('reflection-tone-actual.wav',-80)]:
    a,b=measure(before/name),measure(after/name)
    improvement=a['control_images_dBr']-b['control_images_dBr']
    change=b['rms_dBFS']-a['rms_dBFS']
    result[name]={'before':a,'after':b,'reduction_dB':improvement,'level_change_dB':change}
    assert b['control_images_dBr']<limit, f'{name}: control images exceed limit'
    assert improvement>35, f'{name}: insufficient improvement'
    assert abs(change)<.1, f'{name}: improvement caused by a level change'
result['passed']=True
output.write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2))
