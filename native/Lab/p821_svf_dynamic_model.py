"""Inference-only SVF realization for v31/v33 dynamic-bandwidth checkpoints."""
import p821_dynamic_q_control_model as previous
core=previous.core;prepare_source=previous.prepare_source

class Model(previous.Model):
    filter_realization='svf'
    def forward(self,*args,**kwargs):raise RuntimeError('Inference-only SVF family; use continuous renderer')

def activate():previous.activate();core.VERSION='v31svf';core.Model=Model
