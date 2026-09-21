"""Audit an impulse-fitted compact linear path inside the complete model."""
import argparse,hashlib,importlib,json,time
import numpy as np
from scipy import signal
import torch
from p821_identification_analysis import ROOT,SR,read,metrics
from p821_select_continuous import MODULES
from p821_bla_correction import curves


def compact(x,kernel):
    result=signal.lfilter(kernel['fir'],[1.],x,axis=0)
    for section in kernel['parallel_sos']:
        result+=signal.lfilter(section[:3],section[3:],x,axis=0)
    return result


def main(version):
    torch.set_num_threads(1);extension=importlib.import_module(MODULES[version]);extension.activate()
    path=ROOT/'compact-frequency-head128-modes6.npz';kernel=np.load(path)
    checkpoint=torch.load(ROOT/f'surrogate-{version}-continuous-selected.pt',weights_only=True)
    model=extension.Model();model.load_state_dict(checkpoint['state_dict']);model.eval();rows={}
    for name in ['quality-silence','quality-dc','quality-tones','music-levels','history-plus','detector-pulses']:
        d=extension.prepare_source(json.loads((ROOT/(name+'.json')).read_text())['job']['input'])
        x=d['x'].astype(np.float64);gain,_=curves(x)
        start=time.monotonic();base=compact(x,kernel)*gain;seconds=time.monotonic()-start
        changed={**d,'base':torch.from_numpy(base.astype(np.float32))}
        original=extension.core.predict(model,d);actual=extension.core.predict(model,changed);target=read(name)
        row=dict(compact_vs_original=metrics(original[SR:],actual[SR:]),
                 original_vs_P821=metrics(target[SR:],original[SR:]),compact_vs_P821=metrics(target[SR:],actual[SR:]),
                 compact_linear_seconds=seconds,base_vs_original=metrics(d['base'].numpy(),base),finite=bool(np.isfinite(actual).all()),peak=float(np.max(abs(actual))))
        if name=='quality-silence':assert not np.count_nonzero(actual);row['exact_zero']=True
        if name=='music-levels':row['passages']=[c|metrics(target[c['start']:c['end']],actual[c['start']:c['end']]) for c in json.loads((ROOT/'input-music-levels.json').read_text())]
        rows[name]=row
        report=dict(version=version,step=checkpoint['step'],kernel=path.name,kernel_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                    kernel_fitted_to='impulse response only',final_holdouts_used=False,rows=rows)
        (ROOT/f'compact-pipeline-{version}.json').write_text(json.dumps(report,indent=2))
        print(name,'P821',round(row['original_vs_P821']['residual_dbr'],4),round(row['compact_vs_P821']['residual_dbr'],4),
              'original difference',round(row['compact_vs_original']['residual_dbr'],2),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('version',choices=list(MODULES));main(parser.parse_args().version)
