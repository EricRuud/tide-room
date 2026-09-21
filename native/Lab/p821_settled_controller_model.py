"""Inference-only v31 candidate with deterministic controller state after silence.

The new long-memory reference test found no material effect on a new note after
one second of silence. This candidate settles only the fast GRU after 188 fully
quiet frames. The canonical state is generated from the same weights and 188
quiet frames; it is not fitted to reference output. Width and audio states carry.
"""
import numpy as np
import torch
from torch import nn
import p821_dynamic_q_control_model as previous

core=previous.core;prepare_source=previous.prepare_source
QUIET_FRAMES=188


class SettledGRU(nn.GRU):
    def __init__(self,*args,quiet_level_index=14,**kwargs):
        super().__init__(*args,**kwargs);self.canonical=None;self.quiet_level_index=quiet_level_index
    def forward(self,input,hx=None):
        assert not self.training and not input.requires_grad,'Inference-only controller'
        if self.canonical is None:
            quiet=torch.full((1,QUIET_FRAMES,self.input_size),-3.,dtype=input.dtype);quiet[:,:,self.quiet_level_index]=-2.
            _,self.canonical=nn.GRU.forward(self,quiet)
        outputs=[];ends=[]
        for channel in range(input.shape[0]):
            quiet=((input[channel,:,0]<=-2.99999)&(input[channel,:,6]<=-2.99999)).numpy();settled=np.zeros(len(quiet),dtype=bool);count=0
            for index,value in enumerate(quiet):count=count+1 if value else 0;settled[index]=count>=QUIET_FRAMES
            boundaries=np.r_[0,np.flatnonzero(settled[1:]!=settled[:-1])+1,len(quiet)];pieces=[];state=None if hx is None else hx[:,channel:channel+1]
            for a,b in zip(boundaries[:-1],boundaries[1:]):
                if settled[a]:state=self.canonical;out=state.transpose(0,1).repeat(1,b-a,1)
                else:out,state=nn.GRU.forward(self,input[channel:channel+1,a:b],state)
                pieces.append(out)
            outputs.append(torch.cat(pieces,dim=1));ends.append(state)
        return torch.cat(outputs,dim=0),torch.cat(ends,dim=1)


class Model(previous.Model):
    def __init__(self):super().__init__();self.controller=SettledGRU(61,48,batch_first=True)
    def forward(self,*args,**kwargs):raise RuntimeError('Inference-only settlement candidate; use continuous renderer')


def activate():previous.activate();core.VERSION='v31settled';core.Model=Model
