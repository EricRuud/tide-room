"""A training-only attack/recovery fixture at previously unmeasured settings."""
from p821_900r2_common import *
import p821_measure as bench
from p821_900_width import label

def main():
 torch.set_num_threads(1);bench.ROOT=ROOT;settings={'Formula SW':1,'IPS SW':1,'Stage Focus':1}
 bench.submit('reference-repeat',PARENT/'input-smoke.wav',settings)
 y=sf.read(ROOT/'reference-repeat.wav',always_2d=True)[0];z=parent.read('smoke-90030')
 repeat=metrics(z,y);store('reference-repeat-check.json',repeat);assert repeat['residual_dbr']<-80,repeat
 rng=np.random.default_rng(903211);pieces=[np.zeros((SR,2))];records=[];cursor=SR
 for i,f in enumerate(rng.permutation([43,61,109,233,811,3907,6781,12289,157,1777,4871,9719])):
  duration=[.03,.085,.24,.55][i%4];gap=[.012,.12,.45][i%3];level=[.11,.24,.42,.7][(i//3)%4];attack=[0,.0005,.006][i%3]
  t=np.arange(int(duration*SR))/SR;env=np.ones(len(t));n=int(attack*SR)
  if n:env[:n]=np.linspace(0,1,n)
  carrier=np.sin(2*np.pi*f*t+rng.uniform(-np.pi,np.pi))
  if f<8000:carrier+=.12*np.sin(2*np.pi*f*2.013*t+.4)
  pan=rng.uniform(-.7,.7);g=np.sqrt(np.array([1-pan,1+pan])*.5)
  pulse=level*carrier[:,None]*env[:,None]*g
  ptime=np.arange(int(.32*SR))/SR;probe=.012*np.sin(2*np.pi*1309*ptime)[:,None]*g[::-1]
  segment=np.concatenate([pulse,np.zeros((int(gap*SR),2)),probe,np.zeros((int(.45*SR),2))]);pieces.append(segment)
  records.append({'start':cursor,'end':cursor+len(segment),'frequency':int(f),'peak_scale':level,'duration':duration,'gap':gap,'attack':attack});cursor+=len(segment)
 pieces.append(np.zeros((SR,2)));name='train-edges';source=bench.save(name,np.concatenate(pieces));store('train-edges-recipe.json',{'seed':903211,'training_only':True,'events':records})
 bench.submit(name,source,settings);bench.submit(name+'-focus-zero',source,settings|{'Stage Focus':0})
 full=sf.read(ROOT/f'{name}.wav',always_2d=True)[0];off=sf.read(ROOT/f'{name}-focus-zero.wav',always_2d=True)[0]
 baseline=json.loads((PARENT/'width-calibration.json').read_text())['difference_db'];v,w,report=label(full,off,baseline)
 np.savez_compressed(ROOT/f'width-labels-{name}.npz',delta=v,weight=w);store('train-edges-factorization.json',report)
 print('Training edges captured',len(full)/SR,report,flush=True)

if __name__=='__main__':main()
