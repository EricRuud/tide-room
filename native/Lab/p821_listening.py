"""Fixed loudness matching for the user's original room recording through P821."""
import json
import re
import subprocess
from pathlib import Path
import numpy as np
import soundfile as sf

ROOT=Path(__file__).resolve().parents[2]/'artifacts/p821-analysis-003'
OUT=ROOT/'listen'
OUT.mkdir(exist_ok=True)
SOURCES={
 '01-Original':'input-room.wav',
 '02-P821-Stage-Off':'music-focus-off.wav',
 '03-P821-Stage-Half':'music-focus-half.wav',
 '04-P821-Stage-Full':'music-focus-full.wav',
 '05-P821-Full-Center':'music-center.wav',
 '06-P821-Driven':'music-driven.wav',
 '07-Studio80-Reference':'studio-comparison/studio-0.wav',
 '08-Studio80-Cream':'studio-comparison/studio-1.wav',
}


def main():
    measurements={};audio={}
    for name,source in SOURCES.items():
        path=ROOT/source
        p=subprocess.run(['ffmpeg','-hide_banner','-i',str(path),'-af','ebur128','-f','null','-'],capture_output=True,text=True,check=True)
        lufs=float(re.findall(r'I:\s*(-?[\d.]+) LUFS',p.stderr)[-1])
        x,sr=sf.read(path,always_2d=True)
        assert np.all(np.isfinite(x))
        measurements[name]={'source':source,'input_lufs':lufs,'input_peak':float(np.max(np.abs(x))),'sample_rate':sr}
        audio[name]=x
    # Equal integrated loudness, with one shared target that leaves ample peak room.
    target=min(-23,min(v['input_lufs']-20*np.log10(max(v['input_peak'],1e-20)) - 3 for v in measurements.values()))
    for name,x in audio.items():
        m=measurements[name];gain=10**((target-m['input_lufs'])/20);y=x*gain
        m['fixed_gain_db']=float(20*np.log10(gain));m['matched_peak']=float(abs(y).max())
        assert m['matched_peak']<.71
        sf.write(OUT/(name+'.wav'),y,m['sample_rate'],subtype='PCM_24');audio[name]=y
    names=['01-Original','02-P821-Stage-Off','04-P821-Stage-Full','05-P821-Full-Center']
    sr=measurements[names[0]]['sample_rate'];duration=min(8,min(len(audio[n])/sr for n in names)-2)
    clips=[];timeline=[];cursor=0
    for name in names:
        clip=audio[name][2*sr:int((2+duration)*sr)].copy()
        fade=int(.015*sr);clip[:fade]*=np.linspace(0,1,fade)[:,None];clip[-fade:]*=np.linspace(1,0,fade)[:,None]
        clips.extend([clip,np.zeros((sr,2))]);timeline.append({'start_seconds':cursor,'end_seconds':cursor+duration,'version':name});cursor+=duration+1
    sf.write(OUT/'00-Stereo-Comparison.wav',np.concatenate(clips),sr,subtype='PCM_24')
    (OUT/'matching.json').write_text(json.dumps({'target_lufs':target,'files':measurements,'comparison_timeline':timeline},indent=2))
    print(json.dumps({'target_lufs':target,'timeline':timeline},indent=2))


if __name__=='__main__':main()
