"""Three-constant LF-basis experiments using frozen input-driven controls.

The cheap steady fit approximates the effect of replacing one moving filter;
its score is descriptive. Full continuous waveform validation is separate.
No round-1 final recording enters fitting or this selection.
"""
import argparse,json,time
import numpy as np
import torch
from scipy import optimize
from p821_lf_experiments import ROOT,OLD,SR,CHECKPOINT,model as baseline_model,digest,save_json
from p821_identification_analysis import metrics
import p821_rest_state_model as parent


class Model(parent.Model):
    def configure(self,parameters):self.lf=parameters;return self
    def filter_coefficients(self,values):
        result=super().filter_coefficients(values)
        if not hasattr(self,'lf'):return result
        p=self.lf;gain=values[:,:,0]*p['scale'];q=(torch.tensor(np.log(p['q']),dtype=torch.float64)+values[:,:,5]).exp().clamp(.2,8)
        A=torch.pow(10.,gain/40);omega=2*np.pi*p['frequency']/SR;c=np.cos(omega);alpha=np.sin(omega)/(2*q)
        if p['kind']=='shelf':
            beta=2*torch.sqrt(A)*alpha;a0=(A+1)+(A-1)*c+beta
            b0=A*((A+1)-(A-1)*c+beta)/a0;b1=2*A*((A-1)-(A+1)*c)/a0;b2=A*((A+1)-(A-1)*c-beta)/a0
            a1=-2*((A-1)+(A+1)*c)/a0;a2=((A+1)+(A-1)*c-beta)/a0
        else:
            assert p['kind']=='bell';a0=1+alpha/A;b0=(1+alpha*A)/a0;b1=-2*c/a0;b2=(1-alpha*A)/a0;a1=b1;a2=(1-alpha/A)/a0
        result[0]=torch.stack([b0,b1,b2,a1,a2],dim=2);return result


def response(kind,frequency,q,gain,hz):
    A=10**(gain/40);omega=2*np.pi*frequency/SR;c=np.cos(omega);alpha=np.sin(omega)/(2*q)
    if kind=='shelf':
        beta=2*np.sqrt(A)*alpha;a0=(A+1)+(A-1)*c+beta
        b0=A*((A+1)-(A-1)*c+beta)/a0;b1=2*A*((A-1)-(A+1)*c)/a0;b2=A*((A+1)-(A-1)*c-beta)/a0
        a1=-2*((A-1)+(A+1)*c)/a0;a2=((A+1)+(A-1)*c-beta)/a0
    else:
        a0=1+alpha/A;b0=(1+alpha*A)/a0;b1=-2*c/a0;b2=(1-alpha*A)/a0;a1=b1;a2=(1-alpha/A)/a0
    z=np.exp(-2j*np.pi*hz/SR);return (b0+b1*z+b2*z*z)/(1+a1*z+a2*z*z)


def fit():
    extension,baseline=baseline_model();data=json.loads((ROOT/'tone-analysis-v31rest.json').read_text())['rows'];assert len(data)==84
    hz=np.array([r['frequency'] for r in data]);controls=np.array([r['controls'] for r in data]);gain=controls[:,0];offset=controls[:,5]
    target=np.array([10**(r['reference_gain_db']/20)*np.exp(1j*np.radians(r['reference_phase_deg'])) for r in data])
    old=np.array([10**(r['predicted_gain_db']/20)*np.exp(1j*np.radians(r['predicted_phase_deg'])) for r in data])
    f0=float(baseline.frequency[0].exp());q0=float(baseline.q[0].exp());base=response('shelf',f0,np.clip(q0*np.exp(offset),.2,8),gain,hz)
    score=lambda e:float(20*np.log10(np.sqrt(np.mean(abs(e)**2))))
    candidates=[dict(name='baseline',parameters=None,descriptive_error_db=score((old-target)/abs(target)))]
    for kind in ['shelf','bell']:
        def residual(p):
            f,q,scale=np.exp(p);candidate=old*response(kind,f,np.clip(q*np.exp(offset),.2,8),gain*scale,hz)/base
            e=(candidate-target)/abs(target);return np.r_[e.real,e.imag]
        best=None
        for f,q,scale in [(f0,q0,1),(50.,2.,1.5),(55.,4.,2.)]:
            result=optimize.least_squares(residual,np.log([f,q,scale]),bounds=(np.log([25,.3,.2]),np.log([110,8,4])),max_nfev=300,ftol=1e-10,gtol=1e-10,xtol=1e-10)
            if best is None or result.cost<best.cost:best=result
        f,q,scale=np.exp(best.x);row=dict(name='retuned-'+kind,parameters=dict(kind=kind,frequency=float(f),q=float(q),scale=float(scale)),descriptive_error_db=score(residual(best.x))*1.+10*np.log10(2),evaluations=best.nfev)
        candidates.append(row);print(row,flush=True)
    save_json(ROOT/'lf-basis-fit.json',dict(checkpoint_sha256=digest(CHECKPOINT),target_informed_steady_approximation=True,not_predictive_score=True,candidates=candidates))


def load_candidate(name):
    row=next(c for c in json.loads((ROOT/'lf-basis-fit.json').read_text())['candidates'] if c['name']==name)
    m=Model();m.load_state_dict(torch.load(CHECKPOINT,weights_only=True)['state_dict']);m.eval()
    if row['parameters'] is not None:m.configure(row['parameters'])
    return m


def validate():
    torch.set_num_threads(1);names=[r['name'] for r in json.loads((ROOT/'lf-basis-fit.json').read_text())['candidates']];rows=[]
    import soundfile as sf
    for name in ['validation-rounded','validation-sustain','validation-mixed']:
        d=parent.prepare_source(ROOT/f'input-{name}.wav');target,_=sf.read(ROOT/f'{name}.wav',always_2d=True);results={}
        for candidate in names:
            m=load_candidate(candidate);start=time.monotonic();y=parent.core.predict(m,d);assert np.isfinite(y).all()
            results[candidate]=dict(comparison=metrics(target[SR:],y[SR:]),peak=float(np.max(abs(y))),seconds=time.monotonic()-start)
        rows.append(dict(recording=name,results=results));save_json(ROOT/'lf-basis-validation.json',dict(rows=rows,final_used=False,no_alignment_or_gain_fit=True))
        print(name,{c:round(r['comparison']['residual_dbr'],4) for c,r in results.items()},flush=True);del d;time.sleep(.2)
    print('Mean',{c:np.mean([r['results'][c]['comparison']['residual_dbr'] for r in rows]) for c in names},flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['fit','validate']);args=parser.parse_args();globals()[args.mode]()
