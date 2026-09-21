"""Input-only, full-rate audit of separated research models.

Primary scores use the raw aligned waveform, without fitted gain or EQ.
Final holdouts are deliberately excluded. No model is installed in the app.
"""
import argparse,json,time
import numpy as np
import torch
from p821_identification_analysis import ROOT,SR,read,linear_kernel,linear_render,metrics,db
from p821_candidate_audit import amplitude
import p821_separated_model as core


def audit(version,checkpoint_kind='best',tag=''):
    if version in ['v18w','v19w']:
        if version=='v18w':import p821_memory_control_model as extension
        else:import p821_history_gate_model as extension
        extension.activate()
        from p821_width_separated_model import prepare_source
        core.prepare_source=prepare_source
    if version=='v13':
        import p821_adaptive_clip_model as extension
        extension.activate()
    elif version=='v14':
        import p821_flexible_control_model as extension
        extension.activate()
    elif version=='v15':
        import p821_shelf_control_model as extension
        extension.activate()
    elif version=='v16':
        import p821_peak_control_model as extension
        extension.activate()
    elif version=='v17':
        import p821_staged_clip_model as extension
        extension.activate()
    elif version=='v18':
        import p821_memory_control_model as extension
        extension.activate()
    elif version=='v19':
        import p821_history_gate_model as extension
        extension.activate()
    elif version=='v20':
        import p821_width_separated_model as extension
        extension.activate()
    elif version=='v21':
        import p821_detector_bank_model as extension
        extension.activate()
    elif version=='v22':
        import p821_rich_control_model as extension
        extension.activate()
    elif version=='v23':
        import p821_spectral_control_model as extension
        extension.activate()
    elif version=='v24':
        import p821_recovery_control_model as extension
        extension.activate()
    elif version=='v25':
        import p821_crest_gate_model as extension
        extension.activate()
    elif version=='v26':
        import p821_distilled_control_model as extension
        extension.activate()
    elif version=='v27':
        import p821_refined_distillation_model as extension
        extension.activate()
    elif version=='v28':
        import p821_ac_detector_model as extension
        extension.activate()
    elif version=='v29':
        import p821_capacity_control_model as extension
        extension.activate()
    elif version=='v27ac':
        import p821_ac_refined_model as extension
        extension.activate()
    elif version=='v30':
        import p821_gain_q_control_model as extension
        extension.activate()
    elif version=='v31':
        import p821_dynamic_q_control_model as extension
        extension.activate()
    elif version=='v32':
        import p821_postfilter_control_model as extension
        extension.activate()
    elif version=='v27svf':
        import p821_svf_refined_model as extension
        extension.activate()
    elif version=='v33':
        import p821_state_seeded_model as extension
        extension.activate()
    if version in ['v31svf','v33svf']:
        import p821_svf_dynamic_model as extension
        extension.activate()
    if version in ['v31settled','v31settledall']:
        import importlib
        extension=importlib.import_module('p821_fully_settled_model' if version.endswith('all') else 'p821_settled_controller_model')
        extension.activate()
    if version in ['v31rest','v33rest']:
        import p821_rest_state_model as extension
        extension.activate()
    torch.set_num_threads(2)
    checkpoint=torch.load(ROOT/f'surrogate-{version}-{checkpoint_kind}.pt',weights_only=True)
    model=core.Model();model.load_state_dict(checkpoint['state_dict']);model.eval()
    report=dict(model=version,checkpoint_step=checkpoint['step'],sample_rate=SR,block_size=256,final_holdouts_used=False,continuous_state=bool(getattr(model,'continuous_render',False)))
    for name in ['quality-silence','quality-dc','quality-tones','music-levels','history-plus','saturation-map']:
        source=json.loads((ROOT/(name+'.json')).read_text())['job']['input']
        d=core.prepare_source(source);start=time.monotonic();output=core.predict(model,d);elapsed=time.monotonic()-start
        target=read(name);row=metrics(target[SR:],output[SR:])
        row.update(model_peak=float(np.max(abs(output))),finite=bool(np.isfinite(output).all()),offline_render_seconds=elapsed)
        if name=='quality-silence':row['exact_zero']=bool(np.count_nonzero(output)==0)
        if name=='quality-dc':
            row['windows']=[]
            for a,b in [(4.5,5),(9.5,10),(13,14)]:
                yp=output[round(a*SR):round(b*SR)];yt=target[round(a*SR):round(b*SR)]
                row['windows'].append(dict(start=a,end=b,model_mean=yp.mean(axis=0).tolist(),reference_mean=yt.mean(axis=0).tolist(),model_rms=float(np.sqrt(np.mean(yp**2)))))
        if name=='quality-tones':
            row['tones']=[]
            for case in json.loads((ROOT/'quality-tone-cases.json').read_text()):
                a=case['start']+round(.75*SR);b=case['start']+round(1.25*SR)
                result=case|metrics(target[a:b],output[a:b])
                if case['frequency']==17000:
                    for label,data in [('P821',target),('model',output)]:
                        fundamental=amplitude(data[a:b,0],17000)
                        for frequency in [3000,11000]:
                            result[f'{label}_fold_{frequency}_dbr']=db(amplitude(data[a:b,0],frequency)/fundamental)
                row['tones'].append(result)
        if name=='music-levels':
            base=linear_render(d['x'],linear_kernel())[:len(output)]
            row['passages']=[]
            for case in json.loads((ROOT/'input-music-levels.json').read_text()):
                a,b=case['start'],case['end']
                row['passages'].append(case|{'model':metrics(target[a:b],output[a:b]),'linear':metrics(target[a:b],base[a:b])})
        if name=='saturation-map':
            row['cases']=[]
            inv=np.linalg.inv(d['w']).T;yu=output@inv;yt=target@inv
            for case in json.loads((ROOT/'saturation-map-cases.json').read_text()):
                a=case['start'];b=case['end'];steady=a+int(.5*SR)
                row['cases'].append(case|dict(full=metrics(yt[a:b],yu[a:b]),steady=metrics(yt[steady:b],yu[steady:b]),model_peak=float(np.max(abs(yu[steady:b,0]))),target_peak=float(np.max(abs(yt[steady:b,0])))))
        report[name]=row
        suffix='-'+checkpoint_kind if checkpoint_kind!='best' else ''
        if tag:suffix+='-'+tag
        (ROOT/f'quality-audit-{version}{suffix}.json').write_text(json.dumps(report,indent=2))
        print(name,round(row['residual_dbr'],2),'peak',round(row['model_peak'],6),flush=True)
    print(json.dumps(report,indent=2),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('version',choices=['v12','v13','v14','v15','v16','v17','v18','v19','v20','v21','v22','v23','v24','v25','v26','v27','v27ac','v28','v29','v30','v31','v32','v27svf','v33','v31svf','v33svf','v31settled','v31settledall','v31rest','v33rest','v18w','v19w']);parser.add_argument('--checkpoint-kind',choices=['best','selected','continuous-selected'],default='best');parser.add_argument('--tag',default='');args=parser.parse_args();audit(args.version,args.checkpoint_kind,args.tag)
