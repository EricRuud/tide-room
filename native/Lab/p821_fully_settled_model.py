"""Settle both fast and stereo-width GRUs after sustained complete silence."""
import torch
import p821_settled_controller_model as previous
from p821_settled_controller_model import SettledGRU
from p821_width_model import Model as WidthModel,features
from p821_width_separated_model import WIDTH_CHECKPOINT,sample_width
from p821_identification_analysis import ROOT

core=previous.core;Model=previous.Model;_width=None

@torch.no_grad()
def prepare_source(source):
    global _width
    d=previous.prepare_source(source)
    if _width is None:
        _width=WidthModel();_width.controller=SettledGRU(25,32,batch_first=True,quiet_level_index=24)
        _width.load_state_dict(torch.load(ROOT/WIDTH_CHECKPOINT,weights_only=True)['state_dict']);_width.eval()
    delta=_width(features(d['x'].astype('float64'))[None])[0].numpy();d['extra_width_gain']=sample_width(delta,len(d['x']))
    return d


def activate():previous.activate();core.VERSION='v31settledall';core.prepare_source=prepare_source
