"""Check the native extension when disabled, and measure its offline overhead."""
import json, subprocess, tempfile, time
from pathlib import Path
import numpy as np
from p821_lf_experiments import ROOT, OLD, digest, save_json
from p821_lf_native_check import stress, CONFIG


def main():
    binaries={'original':Path('/private/tmp/tide-p821-native-bench'),
              'extended':Path('/private/tmp/tide-p821-lf-native')}
    rows=[]
    with tempfile.TemporaryDirectory(prefix='p821-lf-port-',dir='/private/tmp') as temp:
        temp=Path(temp);stress().tofile(temp/'input.bin');original=None
        for repeat in range(5):
            modes=[('original',OLD/'native-model-v31rest'),
                   ('extended-disabled',OLD/'native-model-v31rest'),
                   ('extended-enabled',CONFIG)]
            if repeat%2:modes.reverse()
            for mode,config in modes:
                binary=binaries['original' if mode=='original' else 'extended']
                timing=json.loads(subprocess.check_output([str(binary),str(config),str(temp/'input.bin'),str(temp/'output.bin')],text=True))
                y=np.fromfile(temp/'output.bin',dtype='<f8');assert np.isfinite(y).all()
                if mode=='original':
                    if original is None:original=y.copy()
                    assert np.array_equal(y,original)
                elif mode=='extended-disabled':assert np.array_equal(y,original),'Disabled extension changed parent audio'
                rows.append(dict(repeat=repeat,mode=mode,**timing));time.sleep(.2)
    summaries={mode:{key:float(np.median([r[key] for r in rows if r['mode']==mode]))
                    for key in ['seconds','median_block_ms','p99_block_ms']}
               for mode in ['original','extended-disabled','extended-enabled']}
    save_json(ROOT/'native-lf-port-audit.json',dict(rows=rows,medians=summaries,
      disabled_extension_bit_identical=True,binary_sha256={k:digest(v) for k,v in binaries.items()},
      enabled_median_block_overhead_percent=100*(summaries['extended-enabled']['median_block_ms']/summaries['original']['median_block_ms']-1),
      scope='Five alternating-order offline runs on one generated stress input while user works. Not an audio-device deadline test.',final_used=False))
    print(json.dumps(summaries,indent=2),flush=True)


if __name__=='__main__':main()
