"""Trapezoidal SVF realization of our moving bell/shelf responses.

Equations: Andrew Simper, Cytomic, SvfLinearTrapOptimised2.pdf.
This changes time-varying behavior while preserving fixed-parameter responses.
"""
import numpy as np
from numba import njit
import torch
from p821_identification_analysis import SR


@njit(cache=True)
def moving_filter(x,gains,offsets,frequency,q0,shelf,svf):
    output=np.empty_like(x)
    for channel in range(2):
        s1=0.;s2=0.;x1=0.;x2=0.;y1=0.;y2=0.
        for n in range(x.shape[0]):
            block=n//256;fraction=(n%256+1)/256
            gain=gains[channel,block]*(1-fraction)+gains[channel,block+1]*fraction
            offset=offsets[channel,block]*(1-fraction)+offsets[channel,block+1]*fraction
            q=min(8.,max(.2,q0*np.exp(offset)));A=10**(gain/40);sample=x[n,channel]
            if svf:
                g=np.tan(np.pi*frequency/SR);k=1/q
                if shelf:g/=np.sqrt(A);m1=k*(A-1);m2=A*A-1
                else:k/=A;m1=k*(A*A-1);m2=0.
                a1=1/(1+g*(g+k));a2=g*a1;a3=g*a2
                v3=sample-s2;v1=a1*s1+a2*v3;v2=s2+a2*s1+a3*v3
                s1=2*v1-s1;s2=2*v2-s2;value=sample+m1*v1+m2*v2
            else:
                omega=2*np.pi*frequency/SR;alpha=np.sin(omega)/(2*q);c=np.cos(omega)
                if shelf:
                    beta=2*np.sqrt(A)*alpha;a0=(A+1)+(A-1)*c+beta
                    b0=A*((A+1)-(A-1)*c+beta)/a0;b1=2*A*((A-1)-(A+1)*c)/a0;b2=A*((A+1)-(A-1)*c-beta)/a0
                    a1=-2*((A-1)+(A+1)*c)/a0;a2=((A+1)+(A-1)*c-beta)/a0
                else:
                    a0=1+alpha/A;b0=(1+alpha*A)/a0;b1=-2*c/a0;b2=(1-alpha*A)/a0;a1=b1;a2=(1-alpha/A)/a0
                value=b0*sample+b1*x1-a1*y1+b2*x2-a2*y2;x2=x1;x1=sample;y2=y1;y1=value
            output[n,channel]=value
    return output


@torch.no_grad()
def parts(model,d):
    values,cap=model.control_parts(d['control'].transpose(0,1));values=values.numpy();cap=cap.numpy();x=d['base'].double().numpy().copy()
    if values.shape[2]>5:offsets=values[:,:,5:9]
    elif hasattr(model,'offsets'):offsets=model.offsets(torch.from_numpy(values)).numpy()
    else:offsets=np.zeros_like(values[:,:,:4])
    f=model.frequency.exp().clamp(20,18000).numpy();q=model.q.exp().numpy()
    for j in range(4):x=moving_filter(x,values[:,:,j],offsets[:,:,j],float(f[j]),float(q[j]),j==0,True)
    fraction=np.arange(1,257)[None,:,None]/256
    gains=(values[:,:-1,4].T[:,None,:]*(1-fraction)+values[:,1:,4].T[:,None,:]*fraction).reshape(-1,2)
    ceiling=(cap[:,:-1].T[:,None,:]*(1-fraction)+cap[:,1:].T[:,None,:]*fraction).reshape(-1,2)
    return x*10**(gains/20),ceiling
