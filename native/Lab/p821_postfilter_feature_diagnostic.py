"""Case-held-out linear-readout diagnostic for frozen post-filter descriptors.

Labels are non-unique fitted trajectories on synthetic training data, not
measured internal controls. The frozen first pass was trained on this corpus,
including the readout's held-out cases. This is not independent generalization
of the whole model or an emulator score.
"""
import hashlib,json,tempfile
from pathlib import Path
import numpy as np
import soundfile as sf
from scipy import signal
import torch
from p821_identification_analysis import ROOT,SR
from p821_spectral_control_model import prepare_source
from p821_postfilter_features import extra,FIRST_CHECKPOINT


def temporal(x):
    features=[x]
    for tau in [.01,.03,.1]:
        alpha=np.exp(-256/(SR*tau));features.append(signal.lfilter([1-alpha],[1,-alpha],x,axis=0))
    return np.concatenate(features,axis=2)


def main():
    torch.set_num_threads(1);folder=ROOT/'training-trajectories-v23';out=ROOT/'postfilter-features-v27';out.mkdir(exist_ok=True)
    source=json.loads((ROOT/'training-rich.json').read_text())['job']['input'];data=[]
    for path in sorted(folder.glob('case-*.json')):
        meta=json.loads(path.read_text());index=meta['case']['index'];a,b=meta['start'],meta['end'];labels=np.load(path.with_suffix('.npz'))
        with sf.SoundFile(source) as file:file.seek(a);x=file.read(b-a,always_2d=True)
        with tempfile.TemporaryDirectory(prefix='p821-postfeatures-',dir='/private/tmp') as temp:
            audio=Path(temp)/'source.wav';sf.write(audio,x,SR,subtype='FLOAT');d=prepare_source(audio)
        post=extra(d).numpy();raw=d['control'].numpy();assert post.shape[:2]==raw.shape[:2]
        mask=labels['mask'].astype(bool);target=labels['values'].transpose(1,0,2)[mask].reshape(-1,5)
        # Four interleaved case folds; neither channel of a case crosses a fold.
        row=dict(index=index,raw=temporal(raw)[mask].reshape(-1,244),augmented=temporal(np.concatenate([raw,post],axis=2))[mask].reshape(-1,484),target=target)
        data.append(row);np.savez_compressed(out/path.with_suffix('.npz').name,post=post)
        print('Prepared synthetic case',index,flush=True)
    rows=[]
    for fold in range(4):
        test=[d for position,d in enumerate(data) if position%4==fold];train=[d for position,d in enumerate(data) if position%4!=fold]
        y=np.concatenate([d['target'] for d in train]);yt=np.concatenate([d['target'] for d in test]);row=dict(fold=fold,test_cases=[d['index'] for d in test])
        for key in ['raw','augmented']:
            x=np.concatenate([d[key] for d in train]).astype(np.float64);xt=np.concatenate([d[key] for d in test]).astype(np.float64)
            mean=x.mean(axis=0);std=np.maximum(x.std(axis=0),.02);x=(x-mean)/std;xt=(xt-mean)/std
            center=y.mean(axis=0);ridge=len(x)*.01;theta=np.linalg.solve(x.T@x+ridge*np.eye(x.shape[1]),x.T@(y-center));prediction=xt@theta+center
            row[key]=dict(mse=float(np.mean((prediction-yt)**2)),per_control_rmse=np.sqrt(np.mean((prediction-yt)**2,axis=0)).tolist())
            del x,xt
        rows.append(row);print(row,flush=True)
    report=dict(rows=rows,first_pass_checkpoint=FIRST_CHECKPOINT,first_pass_sha256=hashlib.sha256((ROOT/FIRST_CHECKPOINT).read_bytes()).hexdigest(),
                target_informed_latent_labels=True,not_emulator_score=True,final_holdouts_used=False,
                first_pass_training_includes_readout_test_cases=True,folds_apply_only_to_linear_readout=True,
                temporal_descriptor_seconds=[0,.01,.03,.1],ridge_per_training_row=.01,
                raw_mean_mse=float(np.mean([r['raw']['mse'] for r in rows])),augmented_mean_mse=float(np.mean([r['augmented']['mse'] for r in rows])))
    (ROOT/'postfilter-feature-diagnostic.json').write_text(json.dumps(report,indent=2))


if __name__=='__main__':main()
