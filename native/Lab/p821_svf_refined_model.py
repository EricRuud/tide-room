"""No-refit v27 family using trapezoidal state-variable moving filters."""
import p821_crest_gate_model as previous
core=previous.core
prepare_source=previous.prepare_source

class Model(previous.Model):
    filter_realization='svf'
    # Inference is deliberately routed through continuous parts; training this
    # class would require a matching differentiable SVF backward implementation.
    def forward(self,*args,**kwargs):
        raise RuntimeError('Inference-only SVF family; use continuous renderer')

def activate():
    previous.activate();core.VERSION='v27svf';core.Model=Model
