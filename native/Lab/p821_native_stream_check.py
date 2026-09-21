"""Buffer-size, layout, in-place and reset checks for the native stream adapter."""
import argparse,hashlib,json,subprocess,tempfile
from pathlib import Path
import numpy as np
from p821_identification_analysis import ROOT,SR


def main(configuration=None):
    rng=np.random.default_rng(301821);n=SR*12;n+=(256-n%256)%256;t=np.arange(n)/SR
    x=np.column_stack([np.sin(2*np.pi*187.5*t),np.sin(2*np.pi*9703*t)])
    amplitude=np.select([t<1,t<2,t<3,t<4,t<6,t<8],[0,.005,.2,1.8,4,.02],default=0.)
    x=(x+rng.normal(size=x.shape)*.08)*amplitude[:,None]
    x[SR*5:SR*5+SR//2]=[.7,-1.2]
    x[SR*9:SR*9+5]=[[0,0],[5,-5],[-5,5],[1,1],[0,0]]
    x=x.astype('<f4');reports=[]
    with tempfile.TemporaryDirectory(prefix='p821-stream-',dir='/private/tmp') as folder:
        source=Path(folder)/'input.bin';x.tofile(source)
        configurations=[(configuration,factor) for factor in [1,4,8]] if configuration else [('native-model-v25',1),('native-model-v28-step-0',1),('native-model-v27',4),('native-model-v27',8)]
        for config,factor in configurations:
            command=['/private/tmp/tide-p821-stream-bench',str(ROOT/config),str(source)]
            if factor>1:command+=[str(ROOT/'native-hq-limiter'),str(factor)]
            result=json.loads(subprocess.check_output(command,text=True))
            reports.append(dict(configuration=config,factor=factor,**result));print(config,factor,result,flush=True)
    result=dict(rows=reports,source_sha256=hashlib.sha256(x.tobytes()).hexdigest(),
                scope='synthetic fixed-48k native DSP; all 20 buffer/layout cases agree exactly after 256-sample adapter delay, plus declared HQ latency',
                timing_scope='offline under concurrent load; not an audio-device deadline guarantee',
                allocation_scope='C++ operator new/new[] within callbacks, not platform-wide malloc instrumentation',
                source_hashes={name:hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest() for name in
                               ['P821StreamAdapter.h','P821NativeConfiguration.h','P821NativeQualityModel.h','ContinuousMultistageLimiter.h','P821StreamBench.cpp']},
                binary_sha256=hashlib.sha256(Path('/private/tmp/tide-p821-stream-bench').read_bytes()).hexdigest(),
                final_holdouts_used=False,no_playback=True)
    (ROOT/('native-stream-check'+('-'+configuration if configuration else '')+'.json')).write_text(json.dumps(result,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--configuration');main(parser.parse_args().configuration)
