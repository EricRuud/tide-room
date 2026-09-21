"""Characterize incremental stereo matrices, separating carrier radiation.

The quiet matrix is removed separately at each Stage Focus setting. A flat,
real cross response far from the carrier suggests extra audio coupling; it is
not a unique routing identification. No candidate is fitted by this script.
"""
import json
import numpy as np
from p821_identification_analysis import ROOT,SR,read,db


def main():
    cases=json.loads((ROOT/'quiet-operating-cases.json').read_text());rows=[]
    for focus,prefix in [(1,'quiet-operating-large'),(0,'quiet-operating-focus-off'),(.5,'quiet-operating-focus-half')]:
        y=(read(prefix+'-plus')-read(prefix+'-minus'))/.024;a=cases[0]['probe_start'];left,right=y[a];quiet=np.array([[left,right],[right,left]]);quiet/=quiet[0].sum();y=y@np.linalg.inv(quiet).T
        for c in cases:
            a=c['probe_start'];H=np.fft.rfft(y[a:a+12288],axis=0);hz=np.fft.rfftfreq(12288,1/SR);ratios=[]
            for f in [1000,2700,4000,8000,16000]:
                k=np.argmin(abs(hz-f));ratios.append(H[k,1]/H[k,0])
            direct=y[a,1]/y[a,0];extra_side=(1-direct)/(1+direct)
            row=dict(focus=focus,level=c['level'],quiet_matrix=quiet.tolist(),direct_cross_ratio=float(direct),equivalent_extra_side_gain_db=db(abs(extra_side)),cross_frequency_ratios=[dict(real=float(z.real),imag=float(z.imag)) for z in ratios]);rows.append(row)
            print(focus,c['level'],'direct cross',round(direct,6),'equiv side dB',round(row['equivalent_extra_side_gain_db'],6),flush=True)
    (ROOT/'operating-stereo-analysis.json').write_text(json.dumps(dict(diagnostic_only=True,rows=rows),indent=2))


if __name__=='__main__':main()
