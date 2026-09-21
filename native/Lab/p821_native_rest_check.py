"""Small generated waveform check for native resting-state behavior."""
import hashlib,json,subprocess,tempfile
from pathlib import Path
import numpy as np
import soundfile as sf
import torch
from p821_identification_analysis import ROOT,SR,metrics
from p821_native_model_check import configuration


def main():
    torch.set_num_threads(1);extension,model,checkpoint,path,config,_=configuration('v31rest')
    n=SR*14;n+=(-n)%256;t=np.arange(n)/SR;rng=np.random.default_rng(319821)
    x=np.zeros((n,2));signal=np.column_stack([np.sin(2*np.pi*375*t)+.3*np.sin(2*np.pi*9103*t),.6*np.sin(2*np.pi*811*t)])
    for start,duration,level in [(2,.15,2.),(3.15,.4,.5),(5,.15,2.),(6.16,.4,.5),(8,.15,.7),(11,.4,.5)]:
        a=round(start*SR/256)*256;b=a+round(duration*SR/256)*256;x[a:b]=signal[a:b]*level
    # Near the descriptor floor: count quiet blocks from measured RMS, not a
    # hard assumption that every tiny numerical sample is exactly zero.
    x[7*SR:8*SR]=rng.normal(size=(SR,2))*2e-7
    with tempfile.TemporaryDirectory(prefix='p821-native-rest-',dir='/private/tmp') as temporary:
        temp=Path(temporary);sf.write(temp/'source.wav',x,SR,subtype='FLOAT');d=extension.prepare_source(temp/'source.wav');expected=extension.core.predict(model,d)
        d['x'].astype('<f4').tofile(temp/'input.bin');cmd=['/private/tmp/tide-p821-native-bench',str(config),str(temp/'input.bin'),str(temp/'output.bin')]
        benchmark=json.loads(subprocess.check_output(cmd,text=True));actual=np.fromfile(temp/'output.bin',dtype='<f8').reshape(-1,2)[:len(x)]
        comparison=metrics(expected,actual);assert np.isfinite(actual).all() and comparison['peak_error']<3e-6,comparison
        subprocess.check_output(cmd,text=True);assert np.array_equal(actual,np.fromfile(temp/'output.bin',dtype='<f8').reshape(-1,2)[:len(x)])
    result=dict(scope='Native/Python port agreement on generated input; not P821 accuracy',final_holdouts_used=False,comparison=comparison,benchmark=benchmark,reset_exact=True,
        checkpoint_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),configuration_hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in config.glob('*.bin')})
    (ROOT/'native-rest-check.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)

if __name__=='__main__':main()
