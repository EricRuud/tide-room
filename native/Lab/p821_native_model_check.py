"""End-to-end fixed-setting native-rate path versus uninterrupted Python.

Uses our own trained checkpoint and measured full FIR. This comparison tests the
port, not accuracy against P821. No hardware playback or app installation occurs.
"""
import argparse,hashlib,importlib,json,subprocess,tempfile
from pathlib import Path
import numpy as np
from scipy import signal
import torch
from p821_identification_analysis import ROOT,SR,linear_kernel,metrics
from p821_select_continuous import MODULES
from p821_native_controls_check import export
from p821_width_separated_model import WIDTH_CHECKPOINT
from p821_width_identify import FOCUS_FULL_DB,BASE_DIFFERENCE_DB


def configuration(version,checkpoint_kind='continuous-selected'):
    assert version in ['v23','v24','v25','v26','v27','v27ac','v28','v30','v31','v27svf','v33','v31svf','v33svf','v31settled','v31settledall','v31rest','v33rest'],'48-state controller format only'
    torch.set_num_threads(1);extension=importlib.import_module(MODULES[version]);extension.activate()
    checkpoint_path=ROOT/f'surrogate-{version}-{checkpoint_kind}.pt';checkpoint=torch.load(checkpoint_path,weights_only=True)
    model=extension.Model();model.load_state_dict(checkpoint['state_dict']);model.eval()
    suffix='' if checkpoint_kind=='continuous-selected' else '-'+checkpoint_kind
    folder=ROOT/f'native-model-{version}{suffix}';folder.mkdir(exist_ok=True)
    if version.endswith('rest'):(folder/'rest.bin').write_bytes(bytes([1]))
    if version.endswith('svf'):(folder/'svf.bin').write_bytes(bytes([1]))
    width=torch.load(ROOT/WIDTH_CHECKPOINT,weights_only=True);export(folder/'control.bin',checkpoint['state_dict'],width['state_dict'])
    if 'settled' in version or version.endswith('rest'):
        with torch.no_grad():
            quiet=torch.full((1,188,61),-3.);quiet[:,:,14]=-2.
            _,canonical=torch.nn.GRU.forward(model.controller,quiet)
            canonical.numpy().astype('<f4').tofile(folder/'quiet-fast-state.bin')
            if version.endswith(('all','rest')):
                from p821_width_model import Model as WidthModel
                wm=WidthModel();wm.load_state_dict(width['state_dict']);wm.eval()
                quiet=torch.full((1,188,25),-3.);quiet[:,:,24]=-2.
                _,canonical=wm.controller(quiet);canonical.numpy().astype('<f4').tofile(folder/'quiet-width-state.bin')
    h=linear_kernel();w=h[:,:,0]/(h[0,0,0]+h[0,1,0]);base=np.einsum('oa,ain->oin',np.linalg.inv(w),h)
    base.astype('<f8').tofile(folder/'base.bin');bank=[]
    for f,kind in [(100,'lowpass'),(300,'lowpass'),(1000,'lowpass'),(3000,'highpass'),(8000,'highpass')]:
        sos=signal.butter(2,f,btype=kind,fs=SR,output='sos')[0];bank.append(sos[[0,1,2,4,5]])
    for f in [45,150,500,1800,2700,6000,10000,16000]:
        b,a=signal.iirpeak(f,.7,fs=SR);bank.append(np.r_[b,a[1:]])
    np.asarray(bank,dtype='<f8').tofile(folder/'features.bin')
    if version in ['v28','v27ac']:
        hp=signal.butter(2,5.,btype='highpass',fs=SR,output='sos')[0]
        hp[[0,1,2,4,5]].astype('<f8').tofile(folder/'ac.bin')
    if version=='v30':model.shape.detach().numpy().astype('<f8').tofile(folder/'gain-q.bin')
    if version in ['v31','v33','v31svf','v33svf','v31settled','v31settledall','v31rest','v33rest']:
        with (folder/'dynamic-q.bin').open('wb') as f:
            model.q_output.weight.detach().numpy().astype('<f4').tofile(f)
            model.q_output.bias.detach().numpy().astype('<f4').tofile(f)
            model.q_tau.exp().detach().numpy().astype('<f8').tofile(f)
    audio=np.r_[model.frequency.exp().detach().numpy(),model.q.exp().detach().numpy(),float(model.release.exp().detach()),
                w.reshape(-1),FOCUS_FULL_DB/BASE_DIFFERENCE_DB]
    audio.astype('<f8').tofile(folder/'audio.bin')
    slow=np.array(list(json.loads((ROOT/'slow-detector-fit.json').read_text())[1]['parameters'].values()))
    slow.astype('<f8').tofile(folder/'slow.bin')
    return extension,model,checkpoint,checkpoint_path,folder,suffix


def main(version,checkpoint_kind='continuous-selected'):
    extension,model,checkpoint,checkpoint_path,folder,suffix=configuration(version,checkpoint_kind);rows=[]
    for name in ['music-levels','history-plus','quality-dc','quality-silence']:
        d=extension.prepare_source(json.loads((ROOT/(name+'.json')).read_text())['job']['input'])
        expected=extension.core.predict(model,d)
        with tempfile.TemporaryDirectory(prefix='p821-native-model-',dir='/private/tmp') as temporary:
            temporary=Path(temporary);d['x'].astype('<f4').tofile(temporary/'input.bin')
            cmd=['/private/tmp/tide-p821-native-bench',str(folder),str(temporary/'input.bin'),str(temporary/'output.bin')]
            benchmark=json.loads(subprocess.check_output(cmd,text=True));actual=np.fromfile(temporary/'output.bin',dtype='<f8').reshape(-1,2)[:d['length']]
            comparison=metrics(expected,actual);assert np.isfinite(actual).all() and comparison['peak_error']<1e-5,comparison
            if name=='quality-silence':assert not np.count_nonzero(actual)
            subprocess.check_output(cmd,text=True);assert np.array_equal(actual,np.fromfile(temporary/'output.bin',dtype='<f8').reshape(-1,2)[:d['length']])
        row=dict(dataset=name,port_comparison=comparison,benchmark=benchmark,reset_repeat_exact=True)
        rows.append(row);print(name,comparison,benchmark,flush=True)
        report=dict(version=version,step=checkpoint['step'],sample_rate=SR,block_size=256,native_rate=True,HQ_resampling=False,ac_coupled_fast_descriptors=version in ['v28','v27ac'],
                    benchmark_scope='complete DSP computation; offline benchmark excludes host/audio-device scheduling and file I/O',
                    exact_FIR_retained=True,rounding_difference='native applies gentle gain before one float32 cast; Python preparation has an additional float32 gain correction',
                    slow_window_rounding='46.9999999812 rounds to 47 frames',final_holdouts_used=False,
                    checkpoint_sha256=hashlib.sha256(checkpoint_path.read_bytes()).hexdigest(),
                    source_hashes={name:hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest() for name in
                                   ['P821FeatureExtractor.h','P821ControlModel.h','P821ChannelAudio.h','P821PartitionedBase.h','P821NativeModel.h','P821NativeConfiguration.h','P821NativeBench.cpp']},
                    native_binary_sha256=hashlib.sha256(Path('/private/tmp/tide-p821-native-bench').read_bytes()).hexdigest(),
                    configuration_hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.glob('*.bin')},rows=rows)
        (ROOT/f'native-model-{version}{suffix}-check.json').write_text(json.dumps(report,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('version',choices=list(MODULES));parser.add_argument('--checkpoint-kind',default='continuous-selected')
    args=parser.parse_args();main(args.version,args.checkpoint_kind)
