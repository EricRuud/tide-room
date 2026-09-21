"""Refine the width controller with the additional randomized training sweep."""
import torch
import p821_width_model as core
from p821_identification_analysis import ROOT


def initialize(m):
    checkpoint=torch.load(ROOT/'width-surrogate-best.pt',weights_only=True)
    m.load_state_dict(checkpoint['state_dict']);print('Width warm start',checkpoint['step'],flush=True)


if __name__=='__main__':
    core.VERSION='width-surrogate-v2';core.EXTRA_TRAINING=['width-training']
    core.train(16000,learning_rate=.0005,initializer=initialize)
