"""Compare allocation-free C++ features with the independent Python path."""
import argparse, hashlib, json, subprocess, tempfile
from pathlib import Path
import numpy as np
from scipy import signal
import soundfile as sf
import torch
from p821_identification_analysis import ROOT, SR
from p821_spectral_control_model import prepare_source
from p821_width_model import features


def main(ac=False):
    torch.set_num_threads(1); rng=np.random.default_rng(821); n=256*4096; t=np.arange(n)/SR
    x=np.column_stack([.7*np.sin(2*np.pi*71*t),.4*np.sin(2*np.pi*211*t)])
    x[SR:3*SR]=rng.normal(size=(2*SR,2))*.4
    x[4*SR:5*SR,1]=0;x[6*SR:8*SR]=.7;x[9*SR:10*SR]=-.7
    x[11*SR:12*SR]=rng.normal(size=(SR,2))*1.7
    x[:256*64]=0;x[13*SR:]=0;x[14*SR,0]=1.8;x[14*SR+317,1]=-.9
    x=x.astype(np.float32);bank=[]
    for f,kind in [(100,'lowpass'),(300,'lowpass'),(1000,'lowpass'),(3000,'highpass'),(8000,'highpass')]:
        sos=signal.butter(2,f,btype=kind,fs=SR,output='sos')[0];bank.append(sos[[0,1,2,4,5]])
    for f in [45,150,500,1800,2700,6000,10000,16000]:
        b,a=signal.iirpeak(f,.7,fs=SR);bank.append(np.r_[b,a[1:]])
    with tempfile.TemporaryDirectory(prefix='p821-native-features-',dir='/private/tmp') as folder:
        folder=Path(folder);source=folder/'input.wav';sf.write(source,x,SR,subtype='FLOAT')
        np.asarray(bank,dtype='<f8').tofile(folder/'filters.bin');x.astype('<f4').tofile(folder/'input.bin')
        d=prepare_source(source)
        if ac:
            from p821_detector_placement import change
            d=change(d,5.,2)
        expected=np.column_stack([d['control'][1:].numpy().reshape(-1,122),features(x.astype(np.float64))[1:].numpy()])
        command=['/private/tmp/tide-p821-feature-bench',str(folder/'filters.bin'),str(folder/'input.bin'),str(folder/'output.bin')]
        if ac:
            hp=signal.butter(2,5.,btype='highpass',fs=SR,output='sos')[0]
            hp[[0,1,2,4,5]].astype('<f8').tofile(folder/'ac.bin');command.append(str(folder/'ac.bin'))
        benchmark=json.loads(subprocess.check_output(command,text=True));actual=np.fromfile(folder/'output.bin',dtype='<f4').reshape(-1,148)
        error=abs(actual-expected);maximum=float(error.max());worst=np.unravel_index(np.argmax(error),error.shape)
        assert np.isfinite(actual).all() and maximum < 2e-6,(maximum,worst,actual[worst],expected[worst])
        again=json.loads(subprocess.check_output(command,text=True));assert np.array_equal(actual,np.fromfile(folder/'output.bin',dtype='<f4').reshape(-1,148))
        report=dict(ac_coupled_fast_descriptors=ac,maximum_feature_error=maximum,worst_frame=int(worst[0]),worst_feature=int(worst[1]),
                    exact_float32_fraction=float(np.mean(actual==expected)),finite=True,reset_repeat_exact=True,
                    audio_seconds=n/SR,benchmark=benchmark,repeat_benchmark=again,
                    benchmark_scope='feature extraction only; excludes controller, audio filters and resampling',
                    header_sha256=hashlib.sha256(Path(__file__).with_name('P821FeatureExtractor.h').read_bytes()).hexdigest())
        suffix='-ac5' if ac else ''
        (ROOT/f'native-features-check{suffix}.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--ac',action='store_true');main(parser.parse_args().ac)
