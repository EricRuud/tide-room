"""Continuous-time release between quadratic reconstructions of input samples.

This is a quality experiment, not an identified P821 equation. Reconstruct the
normalized signal with three adjacent samples, split at zeros/extrema, and solve
dE/dt=(abs(x/cap)-E)/tau exactly on each monotone quadratic segment. Instantaneous
attack constrains E >= abs(x/cap). Future input is needed for reconstruction;
the offline renderer includes padding and must not be called zero-latency.
"""
import json,time
import numpy as np
from numba import njit
from scipy import signal
from p821_memory_limiter import render as ordinary
from p821_integrated_memory import integrate
from p821_identification_analysis import ROOT,SR,metrics


@njit(cache=True)
def moments(lam):
    # Integral lambda*exp(-lambda*(1-v))*v**m dv, m=0,1,2.
    result=np.empty(3)
    for m in range(3):
        term=lam/(m+1);value=term
        for k in range(1,12):
            term *= -lam/(m+k+1);value += term
        result[m]=value
    return result[0],result[1],result[2]


@njit(cache=True)
def quadratic_step_precomputed(previous,current,following,state,lam,decay,J0,J1,J2):
    a=previous;c=.5*(following-2*current+previous);b=current-previous-c
    state=max(state,abs(previous))
    # Most oversampled intervals have neither a zero nor an extremum. Their
    # release moments and exponential are identical and are computed once.
    if b*(b+2*c)>=0 and previous*current>=0:
        endpoint=abs(current)
        if endpoint>=abs(previous) and endpoint>=state:return endpoint
        sgn=1. if previous+current>=0 else -1.
        return max(endpoint,state*decay+sgn*(a*J0+b*J1+c*J2))
    points=np.empty(5);points[0]=0.;points[1]=1.;count=2
    if abs(c)>1e-14:
        vertex=-b/(2*c)
        if 0<vertex<1:points[count]=vertex;count+=1
        disc=b*b-4*c*a
        if disc>0:
            q=-.5*(b+np.copysign(np.sqrt(disc),b))
            root=q/c
            if 0<root<1:points[count]=root;count+=1
            if abs(q)>1e-30:
                root=a/q
                if 0<root<1:points[count]=root;count+=1
    elif abs(b)>1e-14:
        root=-a/b
        if 0<root<1:points[count]=root;count+=1
    # At most one vertex and two roots, so tiny insertion sort is sufficient.
    for i in range(1,count):
        value=points[i];j=i-1
        while j>=0 and points[j]>value:points[j+1]=points[j];j-=1
        points[j+1]=value
    for j in range(count-1):
        left=points[j];right=points[j+1];length=right-left
        if length<=1e-14:continue
        mid=(left+right)*.5;sgn=1. if a+b*mid+c*mid*mid>=0 else -1.
        A=sgn*(a+b*left+c*left*left);B=sgn*(b+2*c*left)*length;C=sgn*c*length*length
        local=lam*length;J=moments(local)
        state=state*np.exp(-local)+A*J[0]+B*J[1]+C*J[2]
        state=max(state,abs(a+b*right+c*right*right))
    return state


@njit(cache=True)
def quadratic_step(previous,current,following,state,lam):
    J=moments(lam)
    return quadratic_step_precomputed(previous,current,following,state,lam,np.exp(-lam),J[0],J[1],J[2])


@njit(cache=True)
def envelope(normalized,following,previous,state,lam):
    result=np.empty_like(normalized);final=state.copy();J=moments(lam);decay=np.exp(-lam)
    for c in range(normalized.shape[0]):
        z0=previous[c];e=state[c]
        for n in range(normalized.shape[1]):
            z1=normalized[c,n];z2=normalized[c,n+1] if n+1<normalized.shape[1] else following[c]
            e=quadratic_step_precomputed(z0,z1,z2,e,lam,decay,J[0],J[1],J[2]);result[c,n]=e;z0=z1
        final[c]=e
    return result,final


def render(x,cap,tau,factor=4,chunk=65536,compensated=True,taps_span=64):
    assert factor in [1,2,4,8,16,32] and x.shape==cap.shape and tau>=.0001
    assert taps_span>=32 and taps_span%2==0
    # Retain the half-step integration grid from the preceding experiment.
    upfactor=2*factor;pad=taps_span+64;source=np.pad(x,((pad,pad),(0,0)));ceiling=np.pad(cap,((pad,pad),(0,0)),mode='edge');n=len(source)
    uptaps=signal.firwin(taps_span*upfactor+1,1/upfactor,window=('kaiser',12))
    downtaps=signal.firwin(taps_span*factor+1,1/factor,window=('kaiser',12)) if factor>1 else np.ones(1)
    residual=np.zeros((n+taps_span,x.shape[1]));state=np.zeros(x.shape[1]);previous_x=np.zeros_like(state);previous_z=np.zeros_like(state)
    for start in range(0,n,chunk):
        end=min(start+chunk,n);a=max(0,start-taps_span);b=min(end+taps_span,n)
        up=signal.resample_poly(source[a:b],upfactor,1,axis=0,window=uptaps);offset=(start-a)*upfactor;length=(end-start)*upfactor
        up=np.pad(up,((0,1),(0,0)))[offset:offset+length+1]
        times=np.arange(length+1)/upfactor+start
        cc=np.column_stack([np.interp(times,np.arange(n),ceiling[:,c]) for c in range(x.shape[1])]);z=up/cc
        env,newstate=envelope(z[:-1].T.copy(),z[-1].copy(),previous_z,state,1/(SR*upfactor*tau))
        delta=integrate(up[:-1].T.copy(),env,previous_x,state).T
        state=newstate;previous_x=up[-2].copy();previous_z=z[-2].copy()
        contribution=signal.upfirdn(downtaps,delta,down=factor,axis=0);residual[start:start+len(contribution)]+=contribution
    delay=taps_span//2 if factor>1 else 0;delta=residual[delay:delay+n]
    if compensated:
        # factor=1 has an ill-conditioned inverse at Nyquist; keep it diagnostic.
        assert factor>=2
        grid=np.linspace(0,1,1025);gain=1/np.cos(np.pi*grid/(2*factor));filt=signal.firwin2(129,grid,gain,window=('kaiser',9));filt/=filt.sum()
        delta=signal.fftconvolve(delta,filt[:,None],mode='same',axes=0)
    return (source+delta)[pad:pad+len(x)]


def check():
    rng=np.random.default_rng(821);largest=0.
    # Independent dense quadrature is used only for decreasing segments, where
    # there is no attack constraint crossing; rising segments test exact attack.
    t=np.linspace(0,1,100001)
    for _ in range(50):
        lam=rng.uniform(.0001,.21);a=rng.uniform(.1,2);b=-rng.uniform(0,a*.4);c=-rng.uniform(0,a*.2);state=a+rng.uniform(0,2)
        expected=state*np.exp(-lam)+np.trapezoid(lam*np.exp(-lam*(1-t))*(a+b*t+c*t*t),t)
        actual=quadratic_step(a,a+b+c,a+2*b+4*c,state,lam);largest=max(largest,abs(actual-expected))
        assert abs(quadratic_step(a,a-b-c,a-2*b-4*c,a,lam)-(a-b-c))<1e-12
    assert largest<1e-10
    for triple in [[0,1,0],[1,0,-1],[0,-1,0],[2,-2,2],[1,.2,2]]:
        a,b,c=triple;actual=quadratic_step(a,b,c,abs(a),.002)
        # Explicitly sampled ODE with dense instantaneous attacks, independent
        # of the polynomial moment calculation and root splitting.
        q=.5*(c-2*b+a);p=b-a-q;values=a+p*t+q*t*t;state=abs(a);alpha=np.exp(-.002/(len(t)-1))
        for v in values[1:]:state=max(abs(v),state*alpha+(1-alpha)*abs(v))
        assert abs(actual-state)<2e-7,(triple,actual,state)
    x=rng.normal(size=(35000,2))*.8;cc=np.full_like(x,.795)
    for factor in [2,4]:
        whole=render(x,cc,.0046,factor,chunk=1000000);pieces=render(x,cc,.0046,factor,chunk=4096);error=float(np.max(abs(whole-pieces)));assert error<1e-12
        assert np.array_equal(render(x*.001,cc,.0046,factor),x*.001)
        assert not np.count_nonzero(render(np.zeros_like(x),cc,.0046,factor))
        print(factor,'chunk error',error,'quiet/silence exact',flush=True)
    print('ODE quadrature max error',largest,flush=True)


if __name__=='__main__':check()
