"""Fresh 900/30 reference jobs in an isolated host; preserve older studies."""
import argparse,json
from pathlib import Path
import numpy as np
import soundfile as sf
from scipy import signal
import p821_measure as bench
ROOT=Path(__file__).resolve().parents[2]/'artifacts/p821-90030-001';SR=48000
bench.ROOT=ROOT
BASE={'Formula SW':1,'IPS SW':1,'Stage Focus':1}
def save(name,x):
 p=ROOT/f'input-{name}.wav'
 if not p.exists():sf.write(p,np.asarray(x),SR,subtype='FLOAT')
 return p
def submit(name,x,p=None,**kw):return bench.submit(name,x,BASE|(p or {}),**kw)
def smoke():
 n=SR*12;x=np.zeros((n,2));t=np.arange(SR)/SR
 for i,level in enumerate([.001,.01,.04,.126,.35]):
  a=(i*2+1)*SR;x[a:a+SR]=level*(np.sin(2*np.pi*1000*t)[:,None]*[1,.5]+.2*np.sin(2*np.pi*8000*t)[:,None]*[.4,1])
 path=save('smoke',x)
 submit('smoke-90030',path);submit('smoke-90030-repeat',path)
 submit('smoke-45615',path,{'Formula SW':0,'IPS SW':0});submit('smoke-90030-after-switch',path)
 submit('smoke-bypass',path,{'Bypass':1})
 x=np.zeros((6*SR,2));x[SR,0]=.001;x[int(3.5*SR),1]=.001;p=save('impulse',x)
 submit('impulse-full',p);submit('impulse-repeat',p)
 submit('impulse-half-amplitude',save('impulse-half',x*.5))
 print('Smoke captures complete',flush=True)
def material(seed,seconds=12):
 rng=np.random.default_rng(seed);n=int(seconds*SR)//256*256;x=np.zeros((n,2));cursor=SR//2
 while cursor<n-SR:
  length=min(int(rng.uniform(.15,1.3)*SR),n-cursor);t=np.arange(length)/SR;f=rng.choice([41.203,55,73.416,110,146.832,220,329.628,440,659.255]);kind=int(rng.integers(0,4));decay=rng.uniform(.08,.9)
  env=(1-np.exp(-t/rng.uniform(.0003,.02)))*np.exp(-t/decay)
  if kind==0:y=sum(np.sin(2*np.pi*f*k*t+rng.uniform(-1,1))/(k**1.3) for k in range(1,16) if f*k<18000)
  elif kind==1:y=np.sin(2*np.pi*f*t+2*np.sin(2*np.pi*f*1.4142*t))*np.sin(2*np.pi*f*.37*t)
  elif kind==2:y=signal.sosfilt(signal.butter(2,rng.uniform(1500,13000),fs=SR,output='sos'),rng.normal(size=length))
  else:y=sum(np.sin(2*np.pi*f*k*t)/(k**1.7) for k in [1,2.01,3.97,7.11])
  y*=env;pan=rng.uniform(-.85,.85);g=np.sqrt(np.array([1-pan,1+pan])*.5);amp=10**(rng.uniform(-26,-10)/20);y*=amp/max(np.sqrt(np.mean(y*y)),1e-9)
  x[cursor:cursor+length]+=y[:,None]*g;cursor+=int(rng.uniform(.1,.6)*SR)
 return x
def datasets():
 for family,seed,count in [('train',903001,8),('validation',903002,3)]:
  for i in range(count):
   name=f'{family}-{i}';x=material(seed+100*i)
   x*=10**((-24+6*(i%4))/20)/max(np.sqrt(np.mean(x*x)),1e-12)
   submit(name,save(name,x));
 live,rate=sf.read(ROOT.parent/'821-room-integration/live/live.wav',always_2d=True);assert rate==SR
 x=live[:,32:34];pieces=[]
 for gain in [-6,0,6,12]:pieces.extend([np.zeros((SR,2)),x*10**(gain/20),np.zeros((SR,2))])
 submit('validation-room',save('validation-room',np.concatenate(pieces)))
 # Independent history fixture with tonal/harmonic conditioners and quiet tails.
 pieces=[]
 for f in [55,250,7000]:
  for gap in [.01,.1,1.,5.]:
   x=np.zeros((int((2+gap)*SR),2));t=np.arange(SR//3)/SR;x[:len(t)]=.45*np.sin(2*np.pi*f*t)[:,None]*[1,.6]
   a=SR//3+int(gap*SR);t=np.arange(SR//2)/SR;x[a:a+len(t)]+=.01*np.sin(2*np.pi*997*t)[:,None]*[.6,1];pieces.append(x)
 submit('validation-history',save('validation-history',np.concatenate(pieces)))
def extras():
 submit('impulse-zero',ROOT/'input-impulse.wav',{'Stage Focus':0})
 for family,count in [('train',8),('validation',3)]:
  for i in range(count):
   name=f'{family}-{i}';submit(name+'-focus-zero',ROOT/f'input-{name}.wav',{'Stage Focus':0})
 for name in ['smoke','validation-room','validation-history']:
  submit(name+'-focus-zero',ROOT/f'input-{name}.wav',{'Stage Focus':0})
 x=np.zeros((48000*26,2));levels=[.001,.003,.01,.03,.08,.14,.005,.08,.02,.1,.003,.05]
 for i,level in enumerate(levels):
  a=(1+2*i)*SR;t=np.arange(2*SR)/SR;x[a:a+2*SR]=level*np.sin(2*np.pi*1500*t)[:,None]
 submit('slow-levels',save('slow-levels',x))
 x=np.zeros((SR*12,2));t=np.arange(SR//2)/SR
 for i,amp in enumerate([.03,.1,.006,.14,.01,.07]):
  a=(1+i)*SR;x[a:a+len(t)]=amp*np.sin(2*np.pi*1500*t)[:,None]
 submit('slow-validation',save('slow-validation',x))
def tones():
 for i in range(2):
  rng=np.random.default_rng(903010+i);pieces=[np.zeros((SR,2))]
  frequencies=rng.permutation([37,87,185,400,900,2100,5200,10000,14500])
  for f in frequencies:
   for level in [.04,.18,.4]:
    t=np.arange(int(.65*SR))/SR;y=np.sin(2*np.pi*f*t)
    if i and f<8000:y+=.15*np.sin(2*np.pi*f*2*t+.4)
    ramp=144;y[:ramp]*=np.arange(ramp)/ramp;y[-ramp:]*=np.arange(ramp)[::-1]/ramp
    pieces.extend([level*y[:,None]*([1,1] if i==0 else [1,.47]),np.zeros((int(.2*SR),2))])
  pieces.append(np.zeros((SR,2)));name=f'train-tone-{i}';p=save(name,np.concatenate(pieces));submit(name,p);submit(name+'-focus-zero',p,{'Stage Focus':0})
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('mode',choices=['smoke','datasets','extras','tones']);a=p.parse_args();{'smoke':smoke,'datasets':datasets,'extras':extras,'tones':tones}[a.mode]()
