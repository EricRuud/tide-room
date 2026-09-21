"""Make constant-level listening copies; preserve native renders unchanged."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
import soundfile as sf


def main(source, destination):
    destination.mkdir(parents=True,exist_ok=False)
    rows, montage = [], []
    files=sorted(source.glob('*.wav'))
    assert len(files)==6, 'Expected six native presets'
    for path in files:
        x,sr=sf.read(path);assert sr==48000 and x.shape[1]==2
        assert np.isfinite(x).all() and abs(x).max()<.98
        tail_db=20*np.log10(max(1e-15,np.sqrt(np.mean(x[-4800:]**2))))
        assert tail_db < -100, f'{path.name}: extend the native render to retain its tail'
        rms=np.sqrt(np.mean(x[:round(6.5*sr)]**2))
        gain=min(10**(-22/20)/rms,.90/abs(x).max())
        y=x*gain
        sf.write(destination/path.name,y,sr,subtype='PCM_24')
        hop=2400
        frame_rms=np.array([np.sqrt(np.mean(y[i:i+hop]**2)) for i in range(0,len(y),hop)])
        voiced=np.flatnonzero(frame_rms>1.e-4)
        end=min(len(y),(int(voiced[-1])+1)*hop+round(.5*sr))
        excerpt=y[:end].copy();fade=round(.05*sr)
        excerpt[-fade:]*=np.linspace(1,0,fade)[:,None]
        montage.extend([excerpt,np.zeros((round(.65*sr),2))])
        rows.append(dict(file=path.name,native_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                         native_peak=float(abs(x).max()),native_seconds=len(x)/sr,
                         native_last_100ms_dbfs=float(tail_db),listening_constant_gain=float(gain),
                         sampler_seconds=end/sr))
    sf.write(destination/'listen-all.wav',np.concatenate(montage[:-1]),48000,subtype='PCM_24')
    report=dict(sample_rate=48000,subtype='PCM_24',level_matching='Constant gain per file to -22 dBFS RMS over first 6.5 seconds, with 0.90 peak ceiling',
                sampler='Six patches in filename order. Excerpts keep 0.5 seconds after the last 50-ms frame above -80 dBFS RMS; a final 50-ms fade and 0.65-second gaps are used only in the sampler.',cases=rows)
    (destination/'levels.json').write_text(json.dumps(report,indent=2)+'\n')
    (destination/'README.md').write_text('# Listen to Tide\n\nPlay **listen-all.wav** for First light, Soft wood, Glass current, Slow bloom, Low tide and Quiet wire, in that order. The individual files keep the longer decays.\n\nThese are constant-level listening copies of renders from the native engine. The standalone and plugin retain their own patch output levels. The sampler shortens very quiet endings and fades its final 50 ms; the individual clips preserve their full generated duration. Exact gains and source hashes are in `levels.json`.\n')
    print('Created',destination,'; sampler seconds:',sum(len(x) for x in montage[:-1])/48000)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('destination',type=Path)
    a=p.parse_args();main(a.source,a.destination)
