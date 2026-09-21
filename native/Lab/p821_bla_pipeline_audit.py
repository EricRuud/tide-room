"""Evaluate the independently identified weak branch inside a full candidate.

The .004/.04 flat-multisine kernels are fixed before this audit. No fitting to
the evaluated music occurs here. Extrapolation of their drive law is explicitly
tested, and the final musical holdouts are excluded.
"""
import argparse,importlib,json
import numpy as np
import torch
from scipy import signal
from p821_identification_analysis import ROOT,SR,read,metrics
from p821_bla_correction import curves
from p821_slow_detector import gain_signal


def corrected(d,ceiling=None,gain_floor=False):
    k=np.load(ROOT/'quiet-bla-kernels.npz');x=d['x'].astype(np.float64);gain,drive=curves(x)
    if not gain_floor:gain=gain_signal(x)[:,None]
    if ceiling is not None:drive=np.minimum(drive,ceiling)
    source=x*drive
    y=np.column_stack([signal.fftconvolve(x[:,c],k['base'])[:len(x)]+signal.fftconvolve(source[:,c],k['correction'])[:len(x)] for c in range(2)])
    return {**d,'base':torch.from_numpy((y*gain).astype(np.float32))}


def main(version):
    module={'v20':'p821_width_separated_model','v22':'p821_rich_control_model','v23':'p821_spectral_control_model'}[version];extension=importlib.import_module(module);extension.activate();core=extension.core;torch.set_num_threads(2)
    checkpoint=torch.load(ROOT/f'surrogate-{version}-continuous-selected.pt',weights_only=True);m=core.Model();m.load_state_dict(checkpoint['state_dict']);m.eval();rows={}
    for name in ['music-levels','history-plus','quality-tones','quality-dc','detector-pulses']:
        d=core.prepare_source(json.loads((ROOT/(name+'.json')).read_text())['job']['input']);target=read(name);variants={'original':d,'weak_unbounded':corrected(d),'weak_capped_drive_db065':corrected(d,.65),'weak_floor90':corrected(d,gain_floor=True)};result={}
        for label,data in variants.items():
            output=core.predict(m,data);row=metrics(target[SR:],output[SR:]);row['finite']=bool(np.isfinite(output).all());row['peak']=float(np.max(abs(output)))
            if name=='music-levels':row['passages']=[c|metrics(target[c['start']:c['end']],output[c['start']:c['end']]) for c in json.loads((ROOT/'input-music-levels.json').read_text())]
            result[label]=row
        rows[name]=result;print(name,{k:round(v['residual_dbr'],3) for k,v in result.items()},flush=True)
        if name=='music-levels':print('passages',{k:[round(v['residual_dbr'],3) for v in row['passages']] for k,row in result.items()},flush=True)
        (ROOT/f'quiet-bla-pipeline-{version}.json').write_text(json.dumps(dict(version=version,step=checkpoint['step'],fitted_on_music=False,final_holdouts_used=False,results=rows),indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('version',choices=['v20','v22','v23']);main(parser.parse_args().version)
