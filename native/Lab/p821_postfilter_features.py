"""Additional input-only descriptors after a frozen v27 filter approximation.

This is a two-pass feedforward hypothesis, not feedback from reference audio or
a claim about the vendor's detector. The frozen first pass has continuous state.
"""
import torch
from p821_identification_analysis import ROOT
from p821_crest_gate_model import Model as FirstModel
from p821_spectral_control_model import augment
from p821_detector_placement import fast_controls
from p821_stateful_render import parts

FIRST_CHECKPOINT='surrogate-v27-continuous-selected.pt'
_first=None


def first_model():
    global _first
    if _first is None:
        _first=FirstModel();_first.load_state_dict(torch.load(ROOT/FIRST_CHECKPOINT,weights_only=True)['state_dict']);_first.eval()
        for p in _first.parameters():p.requires_grad_(False)
    return _first


@torch.no_grad()
def extra(d):
    pre,_=parts(first_model(),d)
    control=fast_controls(pre,d['control'][:,:,14:15]);control=augment(dict(x=pre,control=control))['control']
    # Slow shared level is unchanged and already present in the raw descriptors.
    return torch.cat([control[:,:,:14],control[:,:,15:]],dim=2)


def add(d):return {**d,'control':torch.cat([d['control'],extra(d)],dim=2)}


def check():
    import hashlib,json,tempfile
    from pathlib import Path
    import numpy as np
    import soundfile as sf
    from p821_identification_analysis import SR
    from p821_spectral_control_model import prepare_source
    torch.set_num_threads(1);rng=np.random.default_rng(33821);n=256*188;t=np.arange(n)/SR
    x=np.column_stack([.7*np.sin(2*np.pi*187.5*t),.4*np.sin(2*np.pi*9000*t)]);x[:4096]=0
    altered=x.copy();boundary=256*100;altered[boundary:]=rng.normal(size=altered[boundary:].shape)*1.5
    with tempfile.TemporaryDirectory(prefix='p821-postcheck-',dir='/private/tmp') as folder:
        path=Path(folder)/'source.wav';outputs=[]
        for source in [x,altered,np.zeros((4096,2))]:
            sf.write(path,source,SR,subtype='FLOAT');outputs.append(extra(prepare_source(path)).numpy())
    error=float(np.max(abs(outputs[0][:101]-outputs[1][:101])));assert error<2e-5,error
    assert np.isfinite(outputs[0]).all() and np.all(outputs[2]==-3.)
    report=dict(first_pass=FIRST_CHECKPOINT,first_pass_sha256=hashlib.sha256((ROOT/FIRST_CHECKPOINT).read_bytes()).hexdigest(),
                future_buffer_perturbation_prefix_error=error,tolerance=2e-5,
                exact_silence_descriptors=True,finite=True,final_holdouts_used=False,
                scope='No mathematical future-buffer dependence; FFT-based convolution can differ by numerical rounding')
    (ROOT/'postfilter-features-check.json').write_text(json.dumps(report,indent=2));print(report,flush=True)


if __name__=='__main__':check()
