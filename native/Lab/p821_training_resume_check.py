import contextlib,io,json,tempfile
from pathlib import Path
import numpy as np
import torch
from torch import nn
import p821_separated_model as core

class Toy(nn.Module):
    def __init__(self):
        super().__init__();self.frequency=nn.Parameter(torch.zeros(1));self.q=nn.Parameter(torch.zeros(1));self.tau=nn.Parameter(torch.zeros(1));self.weight=nn.Parameter(torch.randn(1))
    def forward(self,x,c):return x*self.weight+self.frequency+self.q+self.tau

def data(name):
    x=np.arange(48,dtype=np.float32).reshape(24,2)/48
    return dict(name=name,base=torch.tensor(x),control=torch.zeros(13,2,1),target=torch.tensor(.3*x+.2),weight=torch.ones(24,2),starts=np.array([0,2,4,6,8,10,12,14,16]),probability=np.full(9,1/9),y=.3*x+.2,length=24)
core.Model=Toy;core.dataset=data;core.validation_dataset=data;core.predict=lambda m,d:m(d['base'],None).detach().numpy()
core.SR=1;core.BLOCK=2;core.LENGTH=8;core.CONTEXT=4;core.EXTRA_TRAINING=[];core.TRAINING_REPEATS={};core.VERSION='resume-check'
with tempfile.TemporaryDirectory(dir='/private/tmp') as directory:
    root=Path(directory);core.ROOT=root
    path=root/'atomic.json';path.write_text('original')
    def fail(p):p.write_text('partial');raise OSError(28,'Injected storage failure')
    try:core.atomic_write(path,fail)
    except OSError:pass
    assert path.read_text()=='original' and not list(root.glob('*.tmp'))
    with contextlib.redirect_stdout(io.StringIO()):
        core.train(1000,retain_checkpoints=True)
        expected=torch.load(root/'surrogate-resume-check-resume.pt',weights_only=True)
        core.train(500,retain_checkpoints=True)
        core.train(1000,retain_checkpoints=True,resume_from=root/'surrogate-resume-check-resume.pt')
    actual=torch.load(root/'surrogate-resume-check-resume.pt',weights_only=True)
    error=max(float(torch.max(abs(expected['state_dict'][k]-actual['state_dict'][k]))) for k in expected['state_dict'])
    assert error==0,error
    assert expected['numpy_rng']==actual['numpy_rng']
    assert torch.equal(expected['torch_rng'],actual['torch_rng'])
    assert [r['step'] for r in json.loads((root/'surrogate-resume-check-training.json').read_text())]==[0,500,1000]
    print(json.dumps(dict(atomic_failure_preserves_old=True,resume_model_peak_error=error,numpy_rng_exact=True,torch_rng_exact=True)))
