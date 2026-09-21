"""Integrate a linearly reconstructed signal and peak-limiter envelope.

The envelope is evaluated at twice the selected oversampled output rate. On each
centered interval, integrate x/max(1,envelope) analytically for linear endpoints,
splitting at envelope=1. Only the nonlinear residual is downsampled. This reduces
sampling error for that reconstruction, not all aliasing of a bandlimited signal.
"""
import json,time
import numpy as np
from numba import njit
from scipy import signal
from p821_memory_limiter import forward,render as ordinary


@njit(cache=True)
def rational_area(x0,x1,e0,e1):
    u=(e1-e0)/e0
    if abs(u)<1e-4:
        a=1-u/2+u*u/3-u*u*u/4+u**4/5
        b=.5-u/3+u*u/4-u*u*u/5+u**4/6
    else:
        log=np.log1p(u);a=log/u;b=(u-log)/(u*u)
    return (x0*a+(x1-x0)*b)/e0


@njit(cache=True)
def interval_residual(x0,x1,e0,e1):
    if max(e0,e1)<=1:return 0.
    if min(e0,e1)>=1:area=rational_area(x0,x1,e0,e1)
    else:
        t=(1-e0)/(e1-e0);xm=x0*(1-t)+x1*t
        if e0>1:area=t*rational_area(x0,xm,e0,1)+(1-t)*(xm+x1)*.5
        else:area=t*(x0+xm)*.5+(1-t)*rational_area(xm,x1,1,e1)
    return area-(x0+x1)*.5


@njit(cache=True)
def integrate(x,env,previous_x,previous_env):
    result=np.empty((x.shape[0],x.shape[1]//2))
    for c in range(x.shape[0]):
        for n in range(result.shape[1]):
            k=n*2;xl=x[c,k-1] if k else previous_x[c];el=env[c,k-1] if k else previous_env[c]
            result[c,n]=interval_residual(xl,x[c,k+1],el,env[c,k+1])
    return result


def render(x,cap,tau,factor=8,chunk=65536,compensated=False):
    assert factor in [2,4,8,16] and x.shape==cap.shape
    upfactor=2*factor;alpha=np.exp(-1/(48000*upfactor*tau));pad=128 if compensated else 64;source=np.pad(x,((pad,pad),(0,0)));ceiling=np.pad(cap,((pad,pad),(0,0)),mode='edge');n=len(source)
    uptaps=signal.firwin(64*upfactor+1,1/upfactor,window=('kaiser',12));downtaps=signal.firwin(64*factor+1,1/factor,window=('kaiser',12))
    residual=np.zeros((n+64,x.shape[1]));state=np.zeros(x.shape[1]);previous_x=np.zeros_like(state)
    for start in range(0,n,chunk):
        end=min(start+chunk,n);a=max(0,start-64);b=min(end+64,n);up=signal.resample_poly(source[a:b],upfactor,1,axis=0,window=uptaps);up=up[(start-a)*upfactor:(end-a)*upfactor]
        times=np.arange((end-start)*upfactor)/upfactor+start;cc=np.column_stack([np.interp(times,np.arange(n),ceiling[:,c]) for c in range(x.shape[1])])
        _,env,newstate=forward(up.T.copy(),cc.T.copy(),alpha,state)
        delta=integrate(up.T.copy(),env,previous_x,state).T;state=newstate;previous_x=up[-1].copy()
        contribution=signal.upfirdn(downtaps,delta,down=factor,axis=0);residual[start:start+len(contribution)]+=contribution
    delta=residual[32:32+n]
    if compensated:
        # For a locally constant envelope the endpoint-linear integral averages
        # two half-step samples, giving cos(pi*f/(factor*Fs)) droop. Correct only
        # its audible passband, after anti-alias filtering; no unstable inverse.
        grid=np.linspace(0,1,1025);gain=1/np.cos(np.pi*grid/(2*factor))
        compensation=signal.firwin2(129,grid,gain,window=('kaiser',9));compensation/=compensation.sum()
        delta=signal.fftconvolve(delta,compensation[:,None],mode='same',axes=0)
    result=source+delta;return result[pad:pad+len(x)]


def check():
    rng=np.random.default_rng(821);t=np.linspace(0,1,200001);maximum=0.
    for _ in range(80):
        x0,x1=rng.normal(size=2)*2;e0,e1=rng.uniform(.1,3,size=2);x=x0*(1-t)+x1*t;e=e0*(1-t)+e1*t
        numerical=np.trapezoid(x/np.maximum(1,e)-x,t);analytic=interval_residual(x0,x1,e0,e1);maximum=max(maximum,abs(analytic-numerical))
    assert maximum<1e-9
    x=rng.normal(size=(70000,2))*.8;cap=np.ones_like(x)*.795
    for factor in [2,4,8]:
        one=render(x,cap,.0046,factor,chunk=1000000);chunked=render(x,cap,.0046,factor,chunk=16384);error=np.max(abs(one-chunked));assert error<1e-12
        assert np.array_equal(render(x*.001,cap,.0046,factor),x*.001);assert not np.count_nonzero(render(np.zeros_like(x),cap,.0046,factor))
        print(factor,'chunk error',error,'quiet/silence exact',flush=True)
    print('Analytic interval integral quadrature error',maximum,flush=True)


if __name__=='__main__':check()
