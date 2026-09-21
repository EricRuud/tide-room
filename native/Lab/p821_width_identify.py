"""Estimate observable side-gain trajectories from matched Focus renders.

Gain endpoints are fitted to the reference pair, so these are labels/diagnostics,
not input-only predictions. Empty side channels do not supply width observations.
The mid-path comparison and gain-trajectory residual check the factorization.
"""
import json
import numpy as np
from scipy.linalg import solve_banded
from p821_identification_analysis import ROOT,SR,read,metrics

FOCUS_ZERO_DB=.8550377487271432
FOCUS_HALF_DB=1.3838304449754022
FOCUS_FULL_DB=2.239425592983312
BASE_DIFFERENCE_DB=FOCUS_FULL_DB-FOCUS_ZERO_DB


def trajectory(full,zero):
    n=min(len(full),len(zero))//256*256
    sa=(full[:n,0]-full[:n,1])*.5;sb=(zero[:n,0]-zero[:n,1])*.5;frames=n//256
    energy=np.mean(sb.reshape(-1,256)**2,axis=1);weight=sb**2/np.repeat(np.maximum(energy,1e-12),256)
    weight*=np.repeat(energy>1e-8,256)
    valid=(abs(sb)>1e-7)&(sa*sb>0);weight*=valid
    target=20*np.log10(np.maximum(abs(sa),1e-30)/np.maximum(abs(sb),1e-30));target=np.clip(target,-10,10)
    f=np.tile(np.arange(1,257)/256,frames);l=1-f;r=f
    diag=np.zeros(frames+1);rhs=np.zeros(frames+1)
    diag[:-1]+=np.sum((weight*l*l).reshape(-1,256),axis=1);diag[1:]+=np.sum((weight*r*r).reshape(-1,256),axis=1)
    cross=np.sum((weight*l*r).reshape(-1,256),axis=1)
    rhs[:-1]+=np.sum((weight*l*target).reshape(-1,256),axis=1);rhs[1:]+=np.sum((weight*r*target).reshape(-1,256),axis=1)
    regularizer=.01;diag[:-1]+=regularizer;diag[1:]+=regularizer;cross-=regularizer
    diagonal_floor=.001;diag+=diagonal_floor;rhs+=diagonal_floor*BASE_DIFFERENCE_DB
    bands=np.zeros((3,frames+1));bands[1]=diag;bands[0,1:]=cross;bands[2,:-1]=cross
    values=solve_banded((1,1),bands,rhs)
    # No side signal means no identifiable width. A weak baseline prior and a
    # generous observed-range bound prevent extrapolation from near-empty frames.
    values=np.clip(values,BASE_DIFFERENCE_DB-.002,BASE_DIFFERENCE_DB+.4)
    sample_db=(values[:-1,None]*(1-np.arange(1,257)/256)+values[1:,None]*np.arange(1,257)/256).reshape(-1)
    predicted=sb*10**(sample_db/20);weights=np.concatenate([[energy[0]],energy])
    return values,weights,dict(mid=metrics(full[:n].mean(axis=1),zero[:n].mean(axis=1)),side=metrics(sa,predicted),observed_frames=int(np.sum(energy>1e-8))),sample_db


def main():
    sources=[(n,n,'focus-zero-'+n) for n in ['training-noise','training-patterns','training-recovery','training-grid','music-levels']]
    sources += [('structure-'+m,'structure-'+m,'structure-focus-zero-'+m) for m in ['left','side','pan','anti-pan']]
    sources += [('width-levels','width-levels-full','width-levels-zero')]
    report={}
    for name,full_name,zero_name in sources:
        full=read(full_name);zero=read(zero_name);values,weights,row,sample_db=trajectory(full,zero)
        np.savez_compressed(ROOT/f'width-labels-{name}.npz',focus_delta_db=values-BASE_DIFFERENCE_DB,side_energy=weights)
        if name=='structure-pan':
            half=read('structure-focus-half-pan');n=len(sample_db);sb=(zero[:n,0]-zero[:n,1])*.5;fraction=(FOCUS_HALF_DB-FOCUS_ZERO_DB)/BASE_DIFFERENCE_DB
            pred=sb*10**(sample_db*fraction/20);target=(half[:n,0]-half[:n,1])*.5;row['unfitted_half_focus_prediction']=metrics(target,pred)
        report[name]=row;print(name,'side fit',round(row['side']['residual_dbr'],2),'mid dBFS',round(row['mid']['residual_dbfs'],2),flush=True)
        (ROOT/'width-factorization.json').write_text(json.dumps(dict(reference_pair_fits=True,final_holdouts_used=False,rows=report),indent=2))


if __name__=='__main__':main()
