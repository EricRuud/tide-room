"""Full-rate validation and hygiene checks for the v10 research candidate.

No gain, EQ, alignment or time warping is applied to the primary residual score.
The optional gain-aligned score is a separate diagnostic, never the model score.
No final holdout is consumed here.
"""
import argparse
import json
import numpy as np
from p821_identification_analysis import ROOT,SR,read,linear_kernel,linear_render,metrics,db
import p821_bandlimited_dictionary as model


def amplitude(x,frequency):
    t=np.arange(len(x))/SR
    window=np.hanning(len(x))
    return 2*abs(np.sum(x*window*np.exp(-2j*np.pi*frequency*t)))/window.sum()


def audit(ridge):
    report=dict(model='v10',ridge=ridge,sample_rate=SR,block_size=256)
    for name in ['quality-silence','quality-dc','quality-tones','music-levels','history-plus']:
        source=json.loads((ROOT/(name+'.json')).read_text())['job']['input']
        output=model.render(source,None,ridge);target=read(name)
        report[name]=metrics(target[SR:],output[SR:])
        report[name]['model_peak']=float(np.max(abs(output)))
        if name=='quality-silence':
            report[name]['exact_zero']=bool(np.count_nonzero(output)==0)
        if name=='quality-dc':
            report[name]['windows']=[]
            for a,b in [(4.5,5),(9.5,10),(13,14)]:
                yp=output[round(a*SR):round(b*SR)];yt=target[round(a*SR):round(b*SR)]
                report[name]['windows'].append(dict(start=a,end=b,model_mean=yp.mean(axis=0).tolist(),reference_mean=yt.mean(axis=0).tolist(),model_rms=float(np.sqrt(np.mean(yp**2)))))
        if name=='quality-tones':
            report[name]['tones']=[]
            for case in json.loads((ROOT/'quality-tone-cases.json').read_text()):
                a=case['start']+round(.75*SR);b=case['start']+round(1.25*SR)
                row=case|metrics(target[a:b],output[a:b])
                if case['frequency']==17000:
                    for label,data in [('P821',target),('model',output)]:
                        fundamental=amplitude(data[a:b,0],17000)
                        for frequency in [3000,11000]:
                            row[f'{label}_fold_{frequency}_dbr']=db(amplitude(data[a:b,0],frequency)/fundamental)
                report[name]['tones'].append(row)
            # Check that the grouped renderer agrees with the sampled design.
            count=2048;seed=22821
            d=model.prepare(name,count,seed)
            theta=np.load(ROOT/f'surrogate-v10-{ridge:g}.npz')['theta']
            predicted=d['base']+d['A']@theta
            pos=np.sort(np.random.default_rng(seed).choice(np.arange(SR,len(output)),size=count,replace=False))
            h=linear_kernel();w=h[:,:,0]/(h[0,0,0]+h[0,1,0])
            grouped=(output@np.linalg.inv(w).T)[pos].reshape(-1)
            report['grouped_render_design_agreement']=metrics(predicted,grouped)
            del d
        if name=='music-levels':
            base=linear_render(model.raw_input(source),linear_kernel())
            report[name]['passages']=[]
            for case in json.loads((ROOT/'input-music-levels.json').read_text()):
                a,b=case['start'],case['end']
                report[name]['passages'].append(case|{'model':metrics(target[a:b],output[a:b]),'linear':metrics(target[a:b],base[a:b])})
        (ROOT/f'quality-audit-v10-{ridge:g}.json').write_text(json.dumps(report,indent=2))
        print(name,report[name]['residual_dbr'],flush=True)
        del output,target
    print(json.dumps(report,indent=2),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--ridge',type=float,default=.1)
    audit(parser.parse_args().ridge)
