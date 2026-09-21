"""Independent SciPy comparison for native full matrix convolution."""
import hashlib,json,subprocess,tempfile
from pathlib import Path
import numpy as np
from p821_identification_analysis import ROOT,linear_kernel,linear_render


def main():
    rng=np.random.default_rng(821);x=rng.normal(size=(256*1024,2)).astype('<f4')*.2
    x[:200]=0;x[255,0]=1.7;x[256,1]=-.8;x[511,0]=-.9
    rows=[]
    measured=linear_kernel();random=rng.normal(size=(2,2,32761))*np.exp(-np.arange(32761)/4000)[None,None,:]*.001
    for name,kernel in [('measured',measured),('asymmetric',random)]:
        with tempfile.TemporaryDirectory(prefix='p821-native-base-',dir='/private/tmp') as folder:
            folder=Path(folder);kernel.astype('<f8').tofile(folder/'kernel.bin');x.tofile(folder/'input.bin')
            cmd=['/private/tmp/tide-p821-base-bench',str(folder/'kernel.bin'),str(folder/'input.bin'),str(folder/'output.bin')]
            benchmark=json.loads(subprocess.check_output(cmd,text=True));actual=np.fromfile(folder/'output.bin',dtype='<f8').reshape(-1,2)
            expected=linear_render(x.astype(np.float64),kernel);error=float(np.max(abs(actual-expected)));assert error<1e-11,error
            subprocess.check_output(cmd,text=True);assert np.array_equal(actual,np.fromfile(folder/'output.bin',dtype='<f8').reshape(-1,2))
            np.zeros_like(x).tofile(folder/'input.bin');subprocess.check_output(cmd,text=True);assert not np.count_nonzero(np.fromfile(folder/'output.bin',dtype='<f8'))
        row=dict(kernel=name,maximum_error=error,benchmark=benchmark,reset_repeat_exact=True,silence_exact=True)
        rows.append(row);print(row,flush=True)
    (ROOT/'native-base-check.json').write_text(json.dumps(dict(rows=rows,
        scope='full 2x2 32768-tap convolution only; no feature extraction, controller or nonlinear path',
        header_sha256=hashlib.sha256(Path(__file__).with_name('P821PartitionedBase.h').read_bytes()).hexdigest()),indent=2))


if __name__=='__main__':main()
