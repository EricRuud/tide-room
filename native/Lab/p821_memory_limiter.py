"""Instant-attack, signal-following release limiter and an exact local adjoint.

This behavioral hypothesis explains much of the clipped-tone harmonic phase.
It is not a disclosed P821 equation. Oversampled rendering uses explicit state
carry and finite, symmetric FIRs; its offline lookahead is not real-time latency.
"""
import numpy as np
import torch
from numba import njit
from scipy import signal


@njit(cache=True)
def forward(x,cap,alpha,initial):
    y=np.empty_like(x);states=np.empty_like(x);last=initial.copy()
    for channel in range(x.shape[0]):
        env=initial[channel]
        for n in range(x.shape[1]):
            value=abs(x[channel,n])/max(cap[channel,n],1e-9)
            if value>env:env=value
            else:env=alpha*env+(1-alpha)*value
            states[channel,n]=env;y[channel,n]=x[channel,n]/max(1.,env)
        last[channel]=env
    return y,states,last


@njit(cache=True)
def backward(x,cap,alpha,states,upstream):
    dx=np.zeros_like(x);dc=np.zeros_like(cap);da=0.
    for channel in range(x.shape[0]):
        pending=0.
        for n in range(x.shape[1]-1,-1,-1):
            env=states[channel,n];previous=states[channel,n-1] if n>0 else 0.
            ceiling=max(cap[channel,n],1e-9);value=abs(x[channel,n])/ceiling;den=max(1.,env);g=upstream[channel,n]
            dx[channel,n]+=g/den
            local=pending
            if env>1:local-=g*x[channel,n]/(env*env)
            if value>previous:
                dv=local;pending=0.
            else:
                dv=local*(1-alpha);pending=local*alpha;da+=local*(previous-value)
            sign=1. if x[channel,n]>0 else (-1. if x[channel,n]<0 else 0.)
            dx[channel,n]+=dv*sign/ceiling
            if cap[channel,n]>1e-9:dc[channel,n]-=dv*abs(x[channel,n])/(ceiling*ceiling)
    return dx,dc,da


class Limiter(torch.autograd.Function):
    @staticmethod
    def forward(ctx,x,cap,alpha):
        assert x.dtype==cap.dtype==alpha.dtype==torch.float64
        y,states,_=forward(x.detach().numpy(),cap.detach().numpy(),float(alpha.detach()),np.zeros(x.shape[0]))
        state=torch.from_numpy(states);ctx.save_for_backward(x,cap,alpha,state)
        return torch.from_numpy(y)

    @staticmethod
    def backward(ctx,upstream):
        x,cap,alpha,states=ctx.saved_tensors
        dx,dc,da=backward(x.detach().numpy(),cap.detach().numpy(),float(alpha.detach()),states.numpy(),upstream.numpy())
        return torch.from_numpy(dx),torch.from_numpy(dc),torch.tensor(da,dtype=torch.float64)


def render(x,cap,tau,factor=8,chunk=65536,taps_span=64):
    """N-by-channel input. The limiter's envelope state is never reset per chunk."""
    assert x.shape==cap.shape and factor in [1,2,4,8,16,32,64,128]
    assert taps_span>=32 and taps_span%2==0
    alpha=np.exp(-1/(48000*factor*tau))
    if factor==1:return forward(x.T.copy(),cap.T.copy(),alpha,np.zeros(x.shape[1]))[0].T
    pad=taps_span;source=np.pad(x,((pad,pad),(0,0)));ceiling=np.pad(cap,((pad,pad),(0,0)),mode='edge');n=len(source)
    taps=signal.firwin(taps_span*factor+1,1/factor,window=('kaiser',12));residual=np.zeros((n+taps_span,x.shape[1]));state=np.zeros(x.shape[1])
    for start in range(0,n,chunk):
        end=min(start+chunk,n);a=max(0,start-taps_span);b=min(end+taps_span,n)
        up=signal.resample_poly(source[a:b],factor,1,axis=0,window=taps)
        up=up[(start-a)*factor:(end-a)*factor]
        times=np.arange((end-start)*factor)/factor+start
        cc=np.column_stack([np.interp(times,np.arange(n),ceiling[:,c]) for c in range(x.shape[1])])
        out,_,state=forward(up.T.copy(),cc.T.copy(),alpha,state)
        delta=out.T-up
        contribution=signal.upfirdn(taps,delta,up=1,down=factor,axis=0)
        residual[start:start+len(contribution)]+=contribution
    delay=taps_span//2;result=source+residual[delay:delay+n]
    return result[pad:pad+len(x)]


def check():
    torch.set_num_threads(2);torch.manual_seed(821)
    x=(torch.randn(1,32,dtype=torch.float64)*1.5).requires_grad_();c=(torch.rand(1,32,dtype=torch.float64)*.2+.7).requires_grad_();a=torch.tensor(.92,dtype=torch.float64,requires_grad=True)
    assert torch.autograd.gradcheck(Limiter.apply,(x,c,a),eps=1e-6,atol=2e-5,rtol=1e-4)
    rng=np.random.default_rng(821);xx=rng.standard_normal((70000,2))*.8;cap=np.ones_like(xx)*.795
    for factor in [1,2,8]:
        one=render(xx,cap,.0046,factor,chunk=1000000);chunks=render(xx,cap,.0046,factor,chunk=16384)
        error=float(np.max(abs(one-chunks)));assert error<1e-12
        quiet=xx*.001;assert np.array_equal(render(quiet,cap,.0046,factor),quiet)
        assert not np.count_nonzero(render(np.zeros_like(xx),cap,.0046,factor))
        print('factor',factor,'chunk/full error',error,'inactive path exact, silence zero',flush=True)
    print('Memory-limiter adjoint passes finite-difference gradient check',flush=True)


if __name__=='__main__':check()
