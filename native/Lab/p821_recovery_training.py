"""Training-only musical-band probes expose post-transient gain and EQ memory.

Frequencies, levels, and durations differ from the isolated-impulse diagnostics.
Left-only cases preserve the silent opposite channel. Probe audio is never played
through a hardware device; the isolated bench renders files offline.
"""
import json
import numpy as np
from p821_identify import ROOT,SR,save,submit


def main():
    rng=np.random.default_rng(91821);parts=[np.zeros((384*256,2))];cases=[];offset=len(parts[0]);number=0
    for frequency in [125,750,3500,11000]:
        for level in [.55,.95,1.25,1.6]:
            for duration_blocks in [6,18,48,144]:
                n=384*256;t=np.arange(n)/SR;a=48*256;b=a+duration_blocks*256
                probe=sum(np.sin(2*np.pi*f*t+rng.uniform(-np.pi,np.pi)) for f in [997,3011,8009,15013])*.0015
                carrier=np.zeros(n);carrier[a:b]=level*np.sin(2*np.pi*frequency*np.arange(b-a)/SR)
                pan=[1,0] if number%3==0 else ([1,1] if number%3==1 else [1,.6])
                x=(carrier+probe)[:,None]*pan;parts.append(x)
                cases.append(dict(start=offset,end=offset+n,conditioner_start=offset+a,conditioner_end=offset+b,frequency=frequency,level=level,duration_seconds=duration_blocks*256/SR,pan=pan));offset+=n;number+=1
    (ROOT/'training-recovery-cases.json').write_text(json.dumps(cases,indent=2));submit('training-recovery',save('training-recovery',np.concatenate(parts)))


if __name__=='__main__':main()
