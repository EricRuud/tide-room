"""Fresh procedural validation and reserved final inputs for the LF study.

Selection signals are generated before candidate fitting. The final input has a
separate seed/pattern and is captured only after an explicit candidate freeze.
"""
import argparse,json
import numpy as np
from scipy import signal
from p821_lf_experiments import ROOT,OLD,SR,save,submit,save_json,plan,digest


def generate(seed,kind):
    rng=np.random.default_rng(seed);x=np.zeros((14*SR,2))
    cursor=SR;events=[]
    while cursor<12*SR:
        duration=rng.uniform(.11,.65);n=min(round((duration+.5)*SR),len(x)-cursor);t=np.arange(n)/SR
        frequency=float(rng.choice([31.1,38.9,58.3,77.8,116.5,155.6,233.1,311.1]))*rng.uniform(.985,1.015)
        attack=float(rng.choice([.001,.004,.018,.07]));decay=rng.uniform(.04,.6)
        env=(1-np.exp(-t/attack))*np.exp(-t/decay)
        if kind=='sustain':env*=1+.4*np.sin(2*np.pi*rng.uniform(1.3,4.7)*t);env*=np.minimum(1,np.maximum(0,(duration+.4-t)/.1))
        harmonics=np.arange(1,14);weights=1/harmonics**(1.5 if kind=='rounded' else .85)
        phases=rng.uniform(-np.pi,np.pi,len(harmonics));glide=rng.uniform(-.025,.025)
        phase=2*np.pi*frequency*(t+glide*t*t/2)
        voice=(weights[:,None]*np.sin(harmonics[:,None]*phase+phases[:,None])).sum(axis=0)*env
        pan=rng.uniform(-.85,.85);stereo=np.array([np.sqrt((1-pan)/2),np.sqrt((1+pan)/2)])
        amplitude=rng.uniform(.10,.75);x[cursor:cursor+n]+=voice[:,None]*stereo*amplitude
        events.append(dict(sample=cursor,frequency=frequency,amplitude=amplitude,pan=pan,attack=attack,decay=decay))
        cursor+=round(rng.uniform(.09,.40)*SR)
    if kind=='mixed':
        noise=rng.normal(size=x.shape);noise=signal.sosfilt(signal.butter(2,[1300,12000],btype='bandpass',fs=SR,output='sos'),noise,axis=0)
        gate=np.maximum(0,np.sin(2*np.pi*2.7*np.arange(len(x))/SR))**16;x+=noise*gate[:,None]*.025
        x[:SR]=0;x[13*SR:]=0
    peak=float(np.max(abs(x)));x*=.96/peak
    return x,dict(seed=seed,kind=kind,events=events,global_input_gain=.96/peak)


def prepare():
    plan();path=ROOT/'candidate-protocol.json'
    if not path.exists():
        save_json(path,dict(status='defined_before_candidate_fitting',baseline='v31rest step 7500',
          validation=['validation-rounded','validation-sustain','validation-mixed'],
          validation_seeds=[34821,34822,34823],final_name='final-lf-pattern',final_seed=9034821,
          selection='Lowest equal-weight mean raw waveform residual dBr across three new validation clips, after first second, no fitted gain/EQ/delay. Must beat baseline by at least .2 dB.',
          guard='Do not worsen the existing music/history/tones/crest weighted development criterion by more than .15 score units; finite output, exact silence, retained long-silence behavior. Report DC separately.',
          final_audio_read=False,final_reference_captured=False,
          old_final='Round-1 final scores are completed historical results; not used for this candidate selection.',
          resource_policy='Single analysis thread, small batches, no audio-device playback'))
    for seed,kind in [(34821,'rounded'),(34822,'sustain'),(34823,'mixed'),(9034821,'final')]:
        name='final-lf-pattern' if kind=='final' else 'validation-'+kind;x,metadata=generate(seed,kind)
        source=save(name,x);save_json(ROOT/(name+'-source.json'),metadata|dict(input=str(source),sha256=digest(source)))
    print('Fresh validation and reserved final input generated; no final reference captured',flush=True)


def capture():
    prepare()
    for name in ['validation-rounded','validation-sustain','validation-mixed']:submit(name,ROOT/f'input-{name}.wav')


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['prepare','capture']);args=parser.parse_args();globals()[args.mode]()
