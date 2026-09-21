"""Test audio-rate stereo coupling with a loud left carrier and quiet pilots.

Matched +/- probes isolate the incremental response. Taking the output mid
removes the identified final side-width change. Sidebands of an opposite-channel
pilot can reveal fast shared dynamics that a block-rate controller cannot mimic.
"""
import json
import numpy as np
from p821_identify import ROOT,SR,save,submit
from p821_identification_analysis import read,db


def main():
    carriers=[np.zeros((384*256,2))];pilots=[np.zeros_like(carriers[0])];cases=[];offset=len(carriers[0]);probe_frequency=9000
    for frequency in [187.5,1500,6000]:
        for level in [.7,.95,1.3,1.8]:
            for channel in [0,1]:
                n=448*256;a=64*256;b=352*256;t=np.arange(n)/SR;c=np.zeros((n,2));p=np.zeros_like(c)
                c[a:b,0]=level*np.sin(2*np.pi*frequency*np.arange(b-a)/SR);p[:,channel]=.0005*np.sin(2*np.pi*probe_frequency*t)
                carriers.append(c);pilots.append(p);cases.append(dict(start=offset,end=offset+n,conditioner_start=offset+a,conditioner_end=offset+b,carrier_frequency=frequency,carrier_level=level,probe_frequency=probe_frequency,probe_channel=channel));offset+=n
    (ROOT/'fast-link-probe-cases.json').write_text(json.dumps(cases,indent=2));c=np.concatenate(carriers);p=np.concatenate(pilots)
    for sign,label in [(1,'plus'),(-1,'minus')]:submit('fast-link-probe-'+label,save('fast-link-probe-'+label,c+sign*p))
    y=((read('fast-link-probe-plus')-read('fast-link-probe-minus'))*.5).mean(axis=1);rows=[]
    for c in cases:
        windows={}
        for label,offset,length in [('early',2*256,24*256),('steady',128*256,96*256)]:
            a=c['conditioner_start']+offset;b=a+length;spectrum=np.fft.rfft(y[a:b]);index=lambda f:round(f*length/SR);fund=abs(spectrum[index(probe_frequency)]);sidebands=[]
            for k in [-4,-2,2,4]:
                frequency=(probe_frequency+k*c['carrier_frequency'])%SR;frequency=min(frequency,SR-frequency)
                if frequency in [0,probe_frequency]:continue
                sidebands.append(dict(multiple=k,frequency=frequency,relative_dbr=db(abs(spectrum[index(frequency)])/max(fund,1e-30))))
            windows[label]=dict(fundamental_peak=2*fund/length,sidebands=sidebands)
        row=c|dict(windows=windows);rows.append(row);print(c['carrier_frequency'],c['carrier_level'],'probe channel',c['probe_channel'],'steady sidebands',[(v['frequency'],round(v['relative_dbr'],1)) for v in windows['steady']['sidebands']],flush=True)
    (ROOT/'fast-link-probe-analysis.json').write_text(json.dumps(dict(mid_cancels_final_width=True,incremental_response_not_internal_gain=True,rows=rows),indent=2))


if __name__=='__main__':main()
