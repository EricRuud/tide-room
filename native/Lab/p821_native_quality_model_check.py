"""Complete native quality path versus uninterrupted Python at fixed settings."""
import argparse,hashlib,importlib,json,subprocess,tempfile
from pathlib import Path
import numpy as np
import soundfile as sf
import torch
from p821_identification_analysis import ROOT,SR,metrics
from p821_select_continuous import MODULES
from p821_stateful_render import parts,mix
from p821_multistage_limiter import render
from p821_bandlimited_dictionary import raw_input


def main(version):
    assert version in ['v25','v27','v27svf','v28','v30','v31','v33','v31svf','v33svf','v31settled','v31settledall','v31rest','v33rest']
    torch.set_num_threads(1);extension=importlib.import_module(MODULES[version]);extension.activate()
    checkpoint=ROOT/f'surrogate-{version}-continuous-selected.pt';saved=torch.load(checkpoint,weights_only=True)
    model=extension.Model();model.load_state_dict(saved['state_dict']);model.eval();rows=[]
    for name in ['music-levels','history-plus','quality-dc','quality-silence','saturation-map']:
        source=json.loads((ROOT/(name+'.json')).read_text())['job']['input'];x=raw_input(source);length=len(x)
        # Explicit future zeros allow the latency-aligned comparison to include
        # the original final sample without truncating the model's filter tail.
        padded=np.pad(x,((0,512+(-length)%256),(0,0)))
        with tempfile.TemporaryDirectory(prefix='p821-full-quality-',dir='/private/tmp') as folder:
            folder=Path(folder);sf.write(folder/'source.wav',padded,SR,subtype='FLOAT');d=extension.prepare_source(folder/'source.wav')
            pre,cap=parts(model,d);padded.astype('<f4').tofile(folder/'input.bin');release=float(model.release.exp().detach())
            for factor in [4,8]:
                expected=mix(render(pre,cap,release,factor,span=256),d)[:length]
                command=['/private/tmp/tide-p821-native-quality-bench',str(ROOT/f'native-model-{version}'),str(ROOT/'native-hq-limiter'),str(factor),str(folder/'input.bin'),str(folder/'output.bin')]
                benchmark=json.loads(subprocess.check_output(command,text=True));delay=benchmark['latency_samples']
                actual=np.fromfile(folder/'output.bin',dtype='<f8').reshape(-1,2)[delay:delay+length]
                comparison=metrics(expected,actual);assert np.isfinite(actual).all() and comparison['peak_error']<3e-6,comparison
                if name=='quality-silence':assert not np.count_nonzero(actual)
                row=dict(dataset=name,port_comparison=comparison,benchmark=benchmark);rows.append(row);print(name,factor,comparison,benchmark,flush=True)
                report=dict(version=version,step=saved['step'],rows=rows,checkpoint_sha256=hashlib.sha256(checkpoint.read_bytes()).hexdigest(),
                            source_hashes={name:hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest() for name in
                            ['P821NativeModel.h','P821NativeQualityModel.h','P821ChannelAudio.h','ContinuousMultistageLimiter.h','P821NativeQualityBench.cpp']},
                            binary_sha256=hashlib.sha256(Path('/private/tmp/tide-p821-native-quality-bench').read_bytes()).hexdigest(),
                            fixed_sample_rate=SR,block_size=256,final_holdouts_used=False,
                            scope='complete quality DSP port agreement; latency compensated, no fitted gain or EQ',
                            timings_under_concurrent_load=True,host_device_deadlines_untested=True,
                            allocation_scope='C++ new/new[] calls during processing',not_zero_aliasing=True)
                (ROOT/f'native-quality-model-{version}-check.json').write_text(json.dumps(report,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('version',choices=['v25','v27','v27svf','v28','v30','v31','v33','v31svf','v33svf','v31settled','v31settledall','v31rest','v33rest']);main(parser.parse_args().version)
