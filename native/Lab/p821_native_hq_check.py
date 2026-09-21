"""Compare the streaming C++ limiter to the offline Python cascade.

The latency is compensated only for the comparison. Timings are limiter-only.
No speaker playback, reference-plugin invocation or final recordings.
"""
import argparse,hashlib,json,subprocess,tempfile
from pathlib import Path
import numpy as np
from scipy import signal
from p821_identification_analysis import ROOT,SR,metrics
from p821_multistage_limiter import render
from p821_multistage_dynamic_audit import probes


def main(quick=False):
    config=ROOT/'native-hq-limiter';config.mkdir(exist_ok=True)
    signal.firwin(513,.5,window=('kaiser',12)).astype('<f8').tofile(config/'long.bin')
    signal.firwin(65,.5,window=('kaiser',12)).astype('<f8').tofile(config/'short.bin')
    for factor in [4,8]:
        grid=np.linspace(0,1,1025);comp=signal.firwin2(129,grid,1/np.cos(np.pi*grid/(2*factor)),window=('kaiser',9));comp/=comp.sum()
        comp.astype('<f8').tofile(config/f'compensation-{factor}.bin')
    rng=np.random.default_rng(302821);x=rng.normal(size=(SR//2,2))*.9;cap=.795+.12*np.sin(np.arange(len(x))[:,None]/1000)*np.ones((1,2))
    cases=[('broadband-start',x,cap),('quiet',x*.001,cap),('silence',np.zeros_like(x),cap)]
    if not quick:cases+=list(probes())
    rows=[]
    for name,x,cap in cases:
        for factor in [4,8]:
            expected=render(x,cap,.0046,factor,span=256)
            with tempfile.TemporaryDirectory(prefix='p821-native-hq-',dir='/private/tmp') as folder:
                folder=Path(folder);np.column_stack([x,cap]).astype('<f8').tofile(folder/'input.bin')
                result=json.loads(subprocess.check_output(['/private/tmp/tide-continuous-limiter-bench',str(factor),'.0046',str(config),str(folder/'input.bin'),str(folder/'output.bin')],text=True))
                actual=np.fromfile(folder/'output.bin',dtype='<f8').reshape(-1,2)
            comparison=metrics(expected,actual);assert np.isfinite(actual).all()
            row=dict(probe=name,benchmark=result,port_agreement=comparison);rows.append(row);print(name,factor,comparison,result,flush=True)
            report=dict(rows=rows,source_hashes={n:hashlib.sha256(Path(__file__).with_name(n).read_bytes()).hexdigest() for n in
                       ['ContinuousMultistageLimiter.h','ContinuousPeakEnvelope.h','ContinuousLimiterBench.cpp']},
                       binary_sha256=hashlib.sha256(Path('/private/tmp/tide-continuous-limiter-bench').read_bytes()).hexdigest(),
                       scope='limiter-only port comparison, with declared streaming latency compensated',
                       final_holdouts_used=False,not_zero_aliasing=True,timings_under_concurrent_load=True)
            (ROOT/('native-hq-check-quick.json' if quick else 'native-hq-check.json')).write_text(json.dumps(report,indent=2))
            assert comparison['peak_error']<2e-7,comparison
            if name in ['quiet','silence']:assert np.array_equal(actual,x)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--quick',action='store_true');main(parser.parse_args().quick)
