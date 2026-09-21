"""Check the quiet linear baseline across level and position inside a block."""
import json
import numpy as np
from p821_identify import ROOT,SR,save,submit
from p821_identification_analysis import read,linear_kernel,metrics


def main():
    parts=[np.zeros((384*256,2))];cases=[];offset=len(parts[0])
    for amplitude in [.00025,.001,.004,.016,.064]:
        for phase in [0,64,128,192]:
            n=384*256;x=np.zeros((n,2));a=64*256+phase;x[a,0]=amplitude;parts.append(x)
            cases.append(dict(start=offset,end=offset+n,probe_start=offset+a,amplitude=amplitude,phase=phase));offset+=n
    (ROOT/'weak-impulse-map-cases.json').write_text(json.dumps(cases,indent=2));x=np.concatenate(parts)
    for sign,label in [(1,'plus'),(-1,'minus')]:submit('weak-impulse-map-'+label,save('weak-impulse-map-'+label,sign*x))
    y=(read('weak-impulse-map-plus')-read('weak-impulse-map-minus'))*.5;h=linear_kernel();target=h[:,0].T;responses=[];rows=[]
    for c in cases:
        response=y[c['probe_start']:c['probe_start']+len(target)]/c['amplitude'];responses.append(response)
        row=c|dict(comparison=metrics(target,response));rows.append(row);print(c['amplitude'],c['phase'],row['comparison'],flush=True)
    np.savez_compressed(ROOT/'weak-impulse-map-responses.npz',responses=np.array(responses))
    (ROOT/'weak-impulse-map-analysis.json').write_text(json.dumps(rows,indent=2))


if __name__=='__main__':main()
