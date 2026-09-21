"""Refine the accepted bass-limited candidate while preserving its scope."""
import argparse,json
import torch
from p821_lf_gated import initialize
from p821_lf_training import train,check
from p821_lf_select import select
from p821_lf_experiments import ROOT,digest,save_json


def initial():return initialize(step=200,threshold=.55,amount=1.)


def load(step):
    m=initial();saved=torch.load(ROOT/f'gated-lf-step-{step}.pt',weights_only=True);m.load_state_dict(saved['state_dict']);m.configure(saved['basis']);m.eval();return m


def main(mode):
    if mode=='check':check(initializer=initial,prefix='gated-lf')
    elif mode=='train':
        save_json(ROOT/'gated-lf-initialization.json',dict(source='dynamic-lf-step-200.pt',sha256=digest(ROOT/'dynamic-lf-step-200.pt'),threshold=.55,amount=1.,additional_steps=1200))
        train(1200,prefix='gated-lf',initializer=initial,learning_rate=.0005)
    else:select(prefix='gated-lf',loader=load)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('mode',choices=['check','train','select']);main(p.parse_args().mode)
