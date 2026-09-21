"""Small generated-signal check for both new native bandwidth control paths."""
import hashlib,importlib,json,shutil,subprocess,tempfile
from pathlib import Path
import numpy as np
import soundfile as sf
import torch
from p821_identification_analysis import ROOT,SR,metrics
from p821_native_controls_check import export
from p821_width_separated_model import WIDTH_CHECKPOINT
from p821_select_continuous import MODULES


def main():
    torch.set_num_threads(1);rng=np.random.default_rng(30821);t=np.arange(SR*8)/SR
    source=np.column_stack([np.sin(2*np.pi*131*t)+.6*np.sin(2*np.pi*7919*t),.7*np.sin(2*np.pi*67*t)+.3*np.sin(2*np.pi*17003*t)])
    source*=np.select([t<1,t<2,t<3,t<4,t<5,t<6],[0,.005,.3,2.,.05,4.],default=0)[:,None]
    source[SR*6:SR*6+256]=[1.3,-.2];source[SR*5:SR*6]+=.3*rng.normal(size=(SR,2));rows=[]
    width=torch.load(ROOT/WIDTH_CHECKPOINT,weights_only=True)['state_dict']
    for version,kind in [('v27','continuous-selected'),('v30','step-11500'),('v31','step-11000'),('v27svf','continuous-selected')]:
        extension=importlib.import_module(MODULES[version]);extension.activate();source_version='v27' if version=='v27svf' else version;checkpoint_path=ROOT/f'surrogate-{source_version}-{kind}.pt'
        checkpoint=torch.load(checkpoint_path,weights_only=True);model=extension.Model();model.load_state_dict(checkpoint['state_dict']);model.eval()
        with tempfile.TemporaryDirectory(prefix='p821-native-q-',dir='/private/tmp') as temporary:
            temp=Path(temporary);folder=temp/'config';shutil.copytree(ROOT/'native-model-v27',folder)
            export(folder/'control.bin',checkpoint['state_dict'],width)
            if version=='v27svf':(folder/'svf.bin').write_bytes(bytes([1]))
            audio=np.fromfile(folder/'audio.bin',dtype='<f8');audio[:4]=model.frequency.exp().detach().numpy();audio[4:8]=model.q.exp().detach().numpy();audio[8]=float(model.release.exp().detach());audio.tofile(folder/'audio.bin')
            if version=='v30':model.shape.detach().numpy().astype('<f8').tofile(folder/'gain-q.bin')
            if version=='v31':
                with (folder/'dynamic-q.bin').open('wb') as file:
                    model.q_output.weight.detach().numpy().astype('<f4').tofile(file);model.q_output.bias.detach().numpy().astype('<f4').tofile(file);model.q_tau.exp().detach().numpy().astype('<f8').tofile(file)
            sf.write(temp/'source.wav',source,SR,subtype='FLOAT');d=extension.prepare_source(temp/'source.wav');expected=extension.core.predict(model,d)
            d['x'].astype('<f4').tofile(temp/'input.bin');cmd=['/private/tmp/tide-p821-native-bench',str(folder),str(temp/'input.bin'),str(temp/'output.bin')]
            benchmark=json.loads(subprocess.check_output(cmd,text=True));actual=np.fromfile(temp/'output.bin',dtype='<f8').reshape(-1,2)[:len(source)]
            comparison=metrics(expected,actual);assert np.isfinite(actual).all() and comparison['peak_error']<1e-5,comparison
            subprocess.check_output(cmd,text=True);assert np.array_equal(actual,np.fromfile(temp/'output.bin',dtype='<f8').reshape(-1,2)[:len(source)])
            assert not np.count_nonzero(actual[:SR//256*256])
            partial_block_silence_peak=float(np.max(abs(actual[SR//256*256:SR])))
            assert partial_block_silence_peak<1e-12
            row=dict(version=version,step=checkpoint['step'],checkpoint_sha256=hashlib.sha256(checkpoint_path.read_bytes()).hexdigest(),comparison=comparison,benchmark=benchmark,reset_exact=True,complete_silent_blocks_exact=True,partial_block_silence_peak=partial_block_silence_peak)
            rows.append(row);print(json.dumps(row),flush=True)
    (ROOT/'native-bandwidth-check.json').write_text(json.dumps(dict(scope='Port equivalence on generated input, not P821 accuracy',final_holdouts_used=False,rows=rows),indent=2))


if __name__=='__main__':main()
