"""Repeat the sensitive configuration differences and check transparent bypass."""
import argparse,json
import numpy as np
import soundfile as sf
from p821_lf_experiments import ROOT,SR,N,submit,bench,save_json,digest
from p821_lf_clock_check import BASE
from p821_identification_analysis import metrics


def capture():
    for original,block in [('coupling-clock-repeat',256),('coupling-clock-block64',64),('coupling-clock-shift17',256)]:
        for label in ['plus','minus']:
            source=ROOT/f'input-{BASE}-{label}.wav' if original!='coupling-clock-shift17' else ROOT/f'input-{original}-{label}.wav'
            submit(original+'-verify-'+label,source,block=block,warmup=147456/SR)
    for block in [64,256,1024]:
        bench.submit(f'coupling-clock-bypass-{block}',ROOT/f'input-{BASE}-plus.wav',{'Stage Focus':1,'Bypass':1},
          block=block,warmup=147456/SR,warmupInput=False)
    bench.submit('coupling-clock-bypass-shift17',ROOT/'input-coupling-clock-shift17-plus.wav',{'Stage Focus':1,'Bypass':1},
      block=256,warmup=147456/SR,warmupInput=False)


def analyze():
    rows=[];a=11*N;b=14*N;metadata=[]
    for name in ['coupling-clock-repeat','coupling-clock-block64','coupling-clock-shift17']:
        original=[];repeated=[]
        for label in ['plus','minus']:
            y,_=sf.read(ROOT/f'{name}-{label}.wav',always_2d=True);original.append(y)
            y,_=sf.read(ROOT/f'{name}-verify-{label}.wav',always_2d=True);repeated.append(y)
            for variant in [name+'-'+label,name+'-verify-'+label]:
                meta=json.loads((ROOT/f'{variant}.json').read_text())['metadata']
                metadata.append({p['name']:p['value'] for p in meta['parameters'] if p['name'] not in ['InfoDump','Gain Reduction']})
        row=dict(name=name,full_repeat=metrics(original[0][a:b],repeated[0][a:b]),
          marginal_repeat=metrics(.5*(original[0]-original[1])[a:b],.5*(repeated[0]-repeated[1])[a:b]))
        rows.append(row);print(name,'full repeat',row['full_repeat']['residual_dbr'],'marginal repeat',row['marginal_repeat']['residual_dbr'],flush=True)
    assert all(m==metadata[0] for m in metadata),'Public parameters changed'
    bypass=[]
    for name,source in [(f'coupling-clock-bypass-{block}',ROOT/f'input-{BASE}-plus.wav') for block in [64,256,1024]]+[
      ('coupling-clock-bypass-shift17',ROOT/'input-coupling-clock-shift17-plus.wav')]:
        x,rate=sf.read(source,always_2d=True);y,rr=sf.read(ROOT/f'{name}.wav',always_2d=True);assert rate==rr==SR and x.shape==y.shape
        exact=bool(np.array_equal(x[:-52],y[52:]) and not np.count_nonzero(y[:52]));assert exact,name
        bypass.append(dict(name=name,exact_after_known_delay=True,known_delay_samples=52));print(name,'exact with 52-sample delay',flush=True)
    save_json(ROOT/'coupling-clock-verification.json',dict(rows=rows,bypass=bypass,public_parameters_identical=True,
      script_sha256=digest(__file__),scope='Repeated installed-plugin observations in this harness; not independent-host verification or a claim about an internal algorithm.'))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('mode',choices=['capture','analyze']);globals()[p.parse_args().mode]()
