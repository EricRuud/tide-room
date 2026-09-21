"""Inference-only settlement of neural and derived control state after silence.

Threshold: both full-band channel RMS descriptors at their -120 dBFS floor for
188 consecutive frames. The audio path is never reset or muted. Known resting
control states replace arbitrary learned history, using input-only information.
"""
import numpy as np
import torch
from numba import njit
import p821_fully_settled_model as previous
from p821_settled_controller_model import QUIET_FRAMES,SettledGRU
from p821_width_model import Model as WidthModel,features
from p821_width_separated_model import WIDTH_CHECKPOINT,sample_width
from p821_identification_analysis import ROOT,SR

core=previous.core;_width=None


@njit(cache=True)
def mask_from_quiet(quiet):
    mask=np.zeros_like(quiet)
    for c in range(len(quiet)):
        count=0
        for t in range(quiet.shape[1]):count=count+1 if quiet[c,t] else 0;mask[c,t]=count>=QUIET_FRAMES
    return mask


def reset_mask(control):return mask_from_quiet(((control[:,:,0]<=-2.99999)&(control[:,:,6]<=-2.99999)).numpy())


@njit(cache=True)
def peak_with_reset(x,mask):
    y=np.empty_like(x);alpha=np.exp(-256/(SR*.25))
    for c in range(len(x)):
        state=0.
        for t in range(x.shape[1]):state=max(x[c,t],0. if mask[c,t] else alpha*state);y[c,t]=state
    return y


@njit(cache=True)
def reset_smoothing(original,mask,alpha,rest):
    output=original.copy()
    for c in range(original.shape[0]):
        for k in range(original.shape[2]):
            correction=0.
            for t in range(original.shape[1]):
                correction=rest[c,k]-original[c,t,k] if mask[c,t] else correction*alpha[k]
                output[c,t,k]=original[c,t,k]+correction
    return output


class Model(previous.Model):
    def adjust_values(self,values,encoded,control):
        mask=reset_mask(control);activity=[]
        for index in [0,6,15,21]:
            level=torch.pow(10.,(control[:,:,index].double()*30-30)/20).numpy()
            remembered=torch.from_numpy(peak_with_reset(level,mask));activity.append(((remembered-.1)/(.1+remembered)).clamp(0,1))
        own=activity[0];shared=torch.maximum(activity[0],activity[1]);gate=torch.stack([own,own,own,own,shared],dim=2)
        values=(values+12*torch.tanh(self.memory_output(encoded).double())*gate).clamp(-30,30)
        own=torch.relu(activity[2]-activity[0]);shared=torch.relu(torch.maximum(activity[2],activity[3])-torch.maximum(activity[0],activity[1]));gate=torch.stack([own,own,own,own,shared],dim=2)
        return (values+18*torch.tanh(self.crest_output(encoded).double())*gate).clamp(-30,30)
    @torch.no_grad()
    def control_parts(self,control):
        values,cap=super().control_parts(control);mask=reset_mask(control)
        alpha=torch.exp(-256/(SR*torch.cat([self.tau.exp().clamp(.003,.2),self.q_tau.exp().clamp(.003,.2)]))).numpy()
        q=2*torch.tanh(self.q_output(self.controller.canonical[0]).double()).numpy()
        rest=np.tile(np.r_[np.zeros(5),q[0]],(len(values),1))
        values=reset_smoothing(values.numpy(),mask,alpha,rest)
        cap=reset_smoothing(cap.numpy()[:,:,None],mask,np.array([float(torch.exp(-256/(SR*self.ceiling_tau.exp().clamp(.001,.1))))]),np.full((len(cap),1),.795))[:,:,0]
        return torch.from_numpy(values),torch.from_numpy(cap)


@torch.no_grad()
def prepare_source(source):
    global _width
    d=previous.prepare_source(source)
    if _width is None:
        _width=WidthModel();_width.controller=SettledGRU(25,32,batch_first=True,quiet_level_index=24)
        _width.load_state_dict(torch.load(ROOT/WIDTH_CHECKPOINT,weights_only=True)['state_dict']);_width.eval()
    f=features(d['x'].astype('float64'));mask=reset_mask(f[None]);rms=np.sqrt(np.mean(d['x'].astype('float64').reshape(-1,256,2)**2,axis=1));rms=np.vstack([np.zeros((1,2)),rms])
    remembered=peak_with_reset(rms.T,np.repeat(mask,2,axis=0));level=remembered.min(axis=0)
    gate=np.where(level>1e-5,level/(level+.003),0.);f[:,-1]=torch.from_numpy(gate.astype('float32'))
    delta=_width(f[None])[0].numpy();alpha=np.array([float(torch.exp(-256/(SR*_width.tau.exp().clamp(.003,.2))))])
    delta=reset_smoothing(delta[None,:,None],mask,alpha,np.zeros((1,1)))[0,:,0]
    d['extra_width_gain']=sample_width(delta,len(d['x']));return d


def activate():previous.activate();core.VERSION='v31rest';core.Model=Model;core.prepare_source=prepare_source
