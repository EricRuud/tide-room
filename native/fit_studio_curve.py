import numpy as np,json
from scipy.optimize import least_squares
w=np.array([.52,.33,.15]);sens=np.array([1.12,.93,.72]);tau=np.array([1,3,5])*1e-6;phase=2*np.pi*np.arange(4096)/4096;si=np.sin(phase);basis=np.exp(-1j*np.array([1,3])[:,None]*phase);ref=10**(-18/20)
def calc(v):
 k,g,fc=np.exp(v[:3]);a,b=v[3:5];amps=np.exp(v[5:]);pre=lambda f:(1+1j*f*g/fc)/(1+1j*f/fc)
 out=[]
 for amp in amps:
  x=k*sens[:,None]*amp*abs(pre(1000))*si;z=np.tanh(x+a*x**3+b*x**5);spec=2j*np.einsum('jn,hn->hj',z,basis)/len(phase);hh=[abs(np.sum(w*spec[i]/(1+1j*2*np.pi*1000*n*tau))/(k*np.dot(w,sens)*pre(1000*n))) for i,n in enumerate([1,3])];out.extend([hh[0],hh[1]/hh[0]])
 for f in [10000,16000]:out.append(abs(np.sum(w/(1+1j*2*np.pi*f*tau))*4/np.pi/(k*np.dot(w,sens)*pre(f))))
 return np.array(out)
target=np.array([ref,.001,ref*10**(9/20),.01,ref*10**(12.5/20),.03,ref*10**(10.5/20),ref*10**(7.5/20)])
x0=np.r_[np.log([1.1,6.2,22965]),.12,.015,np.log([.13,.38,.6])];lo=np.r_[np.log([.5,2,5000]),0,0,np.log([.08,.25,.5])];hi=np.r_[np.log([2,30,100000]),.3,.3,np.log([.2,.6,1])]
s=least_squares(lambda x:np.log(calc(x)/target),x0,bounds=(lo,hi),xtol=1e-12,gtol=1e-12,ftol=1e-12,max_nfev=300);print(s.success,s.cost);print(np.r_[np.exp(s.x[:3]),s.x[3:5],np.exp(s.x[5:])]);print(calc(s.x)/target)
dictout=dict(k=float(np.exp(s.x[0])),pre_gain=float(np.exp(s.x[1])),fc=float(np.exp(s.x[2])),a=float(s.x[3]),b=float(s.x[4]),amplitudes=np.exp(s.x[5:]).tolist(),relative_fit=(calc(s.x)/target).tolist());open('artifacts/machine-tape-001/sm911-fit-full.json','w').write(json.dumps(dictout,indent=2))
