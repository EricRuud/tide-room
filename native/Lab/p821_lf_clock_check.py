"""Check host block size and sample-shift dependence of a sensitive LF probe.

All comparisons undo only the explicitly inserted integer input shift.
No gain, fitted delay, or model parameters are adjusted.
"""
import argparse,json,subprocess,tempfile
from pathlib import Path
import numpy as np
import soundfile as sf
from p821_lf_experiments import ROOT,OLD,SR,N,save,submit,save_json,digest
from p821_identification_analysis import metrics

BASE='coupling-followup-06-15-05-0600-short'


def capture():
    cases=[dict(name='coupling-clock-repeat',block=256,shift=0),dict(name='coupling-clock-block64',block=64,shift=0),
           dict(name='coupling-clock-block1024',block=1024,shift=0),dict(name='coupling-clock-shift17',block=256,shift=17),dict(name='coupling-clock-shift127',block=256,shift=127)]
    save_json(ROOT/'coupling-clock-cases.json',cases)
    # Equal warm-up sample counts for all three block sizes avoid introducing
    # a different internal-clock phase before the test starts (147456 samples).
    # The original capture used 3 seconds; the repeat quantifies that difference.
    for case in cases:
        for label in ['plus','minus']:
            source=ROOT/f'input-{BASE}-{label}.wav'
            if case['shift']:
                x,rate=sf.read(source,always_2d=True);assert rate==SR;s=case['shift']
                source=save(case['name']+'-'+label,np.pad(x,((s,256-s),(0,0))))
            submit(case['name']+'-'+label,source,block=case['block'],warmup=147456/SR)


def analyze():
    cases=json.loads((ROOT/'coupling-clock-cases.json').read_text());rows=[];base=[];base_input=[]
    for label in ['plus','minus']:
        x,_=sf.read(ROOT/f'input-{BASE}-{label}.wav',always_2d=True);base_input.append(x)
        y,_=sf.read(ROOT/f'{BASE}-{label}.wav',always_2d=True);base.append(y)
    n=len(base[0]);a=11*N;b=14*N;old_marginal=.5*(base[0]-base[1]);new_base=None;native_base=None
    protocol=json.loads((ROOT/'final-lf-protocol.json').read_text());binary=Path(protocol['binaries']['native']);assert digest(binary)==protocol['hashes'][str(binary)]
    with tempfile.TemporaryDirectory(prefix='p821-lf-clock-',dir='/private/tmp') as temp:
        temp=Path(temp)
        for case in cases:
            outputs=[];predictions=[];s=case['shift']
            for i,label in enumerate(['plus','minus']):
                y,_=sf.read(ROOT/f"{case['name']}-{label}.wav",always_2d=True);outputs.append(y[s:s+n])
                x=np.pad(base_input[i],((s,256-s),(0,0))) if s else base_input[i]
                x.astype('<f4').tofile(temp/'input.bin');subprocess.check_output([str(binary),str(ROOT/'native-lf-gated'),str(temp/'input.bin'),str(temp/'output.bin')],text=True)
                predictions.append(np.fromfile(temp/'output.bin',dtype='<f8').reshape(-1,2)[s:s+n])
            marginal=.5*(outputs[0]-outputs[1]);predicted=.5*(predictions[0]-predictions[1])
            if new_base is None:new_base=outputs;new_marginal=marginal;native_base=predictions;native_marginal=predicted
            row=case|dict(reference_full=metrics(new_base[0][a:b],outputs[0][a:b]),
              reference_marginal=metrics(new_marginal[a:b],marginal[a:b]),
              candidate_full=metrics(native_base[0][a:b],predictions[0][a:b]),candidate_marginal=metrics(native_marginal[a:b],predicted[a:b]))
            if not rows:row['against_original_warmup_marginal']=metrics(old_marginal[a:b],marginal[a:b])
            rows.append(row);print(case['name'],'reference marginal',row['reference_marginal']['residual_dbr'],'candidate marginal',row['candidate_marginal']['residual_dbr'],flush=True)
    save_json(ROOT/'coupling-clock-analysis.json',dict(rows=rows,script_sha256=digest(__file__),binary_sha256=digest(binary),
      scope='One sensitive carrier/probe condition. Only specified input shifts undone. Different initial warm-up phase audited separately; no parameter fitting.'))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('mode',choices=['capture','analyze']);globals()[p.parse_args().mode]()
