"""Separate waveform accuracy, harmonic shape, and oversampling quality for v18.

All predictions are input-only. No post-hoc gain or EQ is used in primary scores.
The held-out final musical recordings are not read by this audit.
"""
import argparse,json,time
import numpy as np
import torch
from p821_identification_analysis import ROOT,SR,read,metrics,db
from p821_clip_oversampling import parts
from p821_memory_limiter import render
from p821_stateful_render import mix
from p821_peak_limiter_diagnostic import coefficients
import p821_memory_control_model as extension


def audit(checkpoint_name):
    if 'v29' in checkpoint_name:
        import p821_capacity_control_model as extension
        version='v29'
    elif 'v28' in checkpoint_name:
        import p821_ac_detector_model as extension
        version='v28'
    elif 'v27' in checkpoint_name:
        import p821_refined_distillation_model as extension
        version='v27'
    elif 'v26' in checkpoint_name:
        import p821_distilled_control_model as extension
        version='v26'
    elif 'v25' in checkpoint_name:
        import p821_crest_gate_model as extension
        version='v25'
    elif 'v24' in checkpoint_name:
        import p821_recovery_control_model as extension
        version='v24'
    elif 'v23' in checkpoint_name:
        import p821_spectral_control_model as extension
        version='v23'
    elif 'v22' in checkpoint_name:
        import p821_rich_control_model as extension
        version='v22'
    elif 'v21' in checkpoint_name:
        import p821_detector_bank_model as extension
        version='v21'
    elif 'v20' in checkpoint_name:
        import p821_width_separated_model as extension
        version='v20'
    elif 'v19' in checkpoint_name:
        import p821_history_gate_model as extension
        version='v19'
    else:
        import p821_memory_control_model as extension
        version='v18'
    extension.activate();core=extension.core;torch.set_num_threads(2)
    checkpoint=torch.load(ROOT/checkpoint_name,weights_only=True)
    model=core.Model();model.load_state_dict(checkpoint['state_dict']);model.eval()
    tau=float(torch.clamp(torch.exp(model.release),.0001,.05).detach());report=dict(model=version,checkpoint=checkpoint_name,step=checkpoint['step'],release_seconds=tau,continuous_state=True,final_holdouts_used=False,datasets={})
    for name in ['coherent-clip','clipping-quality']:
        d=core.prepare_source(json.loads((ROOT/(name+'.json')).read_text())['job']['input']);x,cap=parts(model,d);target=read(name);cases=json.loads((ROOT/(name+'-cases.json')).read_text());rows=[]
        for factor in [1,2,4,8,16]:
            start=time.monotonic();prediction=mix(render(x,cap,tau,factor),d)
            if factor==1:assert np.max(abs(prediction-core.predict(model,d)))<1e-12
            row=dict(factor=factor,render_seconds=time.monotonic()-start,overall=metrics(target[SR:],prediction[SR:]),cases=[])
            for case in cases:
                if name=='coherent-clip':
                    a=case['start']+144*256;b=case['end']-24*256
                    result=case|metrics(target[a:b],prediction[a:b])
                    for label,y in [('P821',target),('model',prediction)]:
                        yy=y[a:b]@np.linalg.inv(d['w']).T;z=coefficients(yy[:,0],case['frequency'],a)
                        result[label+'_h3_dbr']=db(abs(z[2]/z[0]));result[label+'_h3_phase_degrees']=float(np.degrees(np.angle(z[2])))
                        result[label+'_peak']=float(np.max(abs(yy[:,0])))
                else:
                    a=case['start']+72*256;b=a+192*256;result=case|metrics(target[a:b],prediction[a:b])
                    if case['low_peak']==0:
                        for label,y in [('P821',target),('model',prediction)]:
                            spec=np.fft.rfft(y[a:b,0]*np.hanning(b-a));index=lambda f:round(f*(b-a)/SR);fund=abs(spec[index(17000)])
                            for f in [3000,11000]:result[f'{label}_fold_{f}_dbr']=db(abs(spec[index(f)])/fund)
                            frequencies=np.array([f for f in range(1000,24000,1000) if f!=17000]);amplitudes=np.array([abs(spec[index(f)]) for f in frequencies]);maximum=int(np.argmax(amplitudes))
                            result[label+'_other_khz_bins_max_dbr']=db(amplitudes[maximum]/fund);result[label+'_other_khz_bins_max_hz']=int(frequencies[maximum])
                row['cases'].append(result)
            rows.append(row);print(name,'factor',factor,'overall',round(row['overall']['residual_dbr'],2),'release ms',round(1000*tau,4),flush=True)
        report['datasets'][name]=rows
        (ROOT/('memory-quality-'+checkpoint_name.removesuffix('.pt')+'-continuous.json')).write_text(json.dumps(report,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--checkpoint',default='surrogate-v18-best.pt');audit(parser.parse_args().checkpoint)
