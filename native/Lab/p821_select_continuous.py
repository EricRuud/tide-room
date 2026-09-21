"""Recompute every selection component with continuous state; preserve old picks."""
import argparse,importlib,json,time
import numpy as np
import torch
from p821_identification_analysis import ROOT,SR,read,metrics
from p821_select_staged import paired_shape

MODULES={'v18':'p821_memory_control_model','v19':'p821_history_gate_model','v20':'p821_width_separated_model','v21':'p821_detector_bank_model','v22':'p821_rich_control_model','v23':'p821_spectral_control_model','v24':'p821_recovery_control_model','v25':'p821_crest_gate_model','v26':'p821_distilled_control_model','v27':'p821_refined_distillation_model'}
MODULES.update(v28='p821_ac_detector_model',v29='p821_capacity_control_model')
MODULES.update(v27ac='p821_ac_refined_model')
MODULES.update(v30='p821_gain_q_control_model')
MODULES.update(v31='p821_dynamic_q_control_model')
MODULES.update(v32='p821_postfilter_control_model')
MODULES.update(v27svf='p821_svf_refined_model')
MODULES.update(v31rest='p821_rest_state_model',v33rest='p821_rest_state_model')
MODULES.update(v31settled='p821_settled_controller_model',v31settledall='p821_fully_settled_model')
MODULES.update(v33='p821_state_seeded_model',v31svf='p821_svf_dynamic_model',v33svf='p821_svf_dynamic_model')


def main(version):
    source_version={'v27ac':'v27','v27svf':'v27','v31svf':'v31','v33svf':'v33','v31settled':'v31','v31settledall':'v31','v31rest':'v31','v33rest':'v33'}.get(version,version.removesuffix('w'))
    extension=importlib.import_module(MODULES[version if version in MODULES else source_version]);extension.activate();core=extension.core;torch.set_num_threads(2)
    if version.endswith('w'):
        from p821_width_separated_model import prepare_source,WIDTH_CHECKPOINT
        core.prepare_source=prepare_source
    plan=json.loads((ROOT/f'selection-plan-{source_version}.json').read_text());training=json.loads((ROOT/f'surrogate-{source_version}-training.json').read_text());assert training[-1]['step']==plan.get('expected_final_step',10000),'Wait for complete training'
    if version.endswith('w'):plan=plan|dict(candidate_family=version,frozen_width_checkpoint=WIDTH_CHECKPOINT,training_source=source_version)
    if version=='v27ac':plan=plan|dict(candidate_family=version,training_source=source_version,
         structural_change='5 Hz second-order high-pass before fast descriptors; no weight refit; raw slow gain and width preserved')
    if version.endswith('svf'):plan=plan|dict(candidate_family=version,training_source=source_version,
         structural_change='Trapezoidal state-variable filters with samplewise linear dB gain; no weight refit')
    if version=='v31settled':plan=plan|dict(candidate_family=version,training_source=source_version,
        structural_change='Fast GRU settles to a canonical quiet state after 188 fully quiet frames; no weight refit')
    if version=='v31settledall':plan=plan|dict(candidate_family=version,training_source=source_version,
        structural_change='Fast and width GRUs settle to canonical quiet states after 188 fully quiet frames; no weight refit')
    if version.endswith('rest'):plan=plan|dict(candidate_family=version,training_source=source_version,
        structural_change='Canonical fast/width GRU, control smoother and peak gate resting states after 188 frames below -120 dBFS; no weight refit')
    data={};targets={}
    for name in ['music-levels','history-plus','quality-tones','crest-validation-plus','crest-validation-minus']:
        data[name]=core.prepare_source(json.loads((ROOT/(name+'.json')).read_text())['job']['input']);targets[name]=read(name)
        # Continuous inference consumes prepared base/control/width arrays.
        # Retaining the original source here only increased memory pressure.
        data[name].pop('x',None)
    target_probe=(targets['crest-validation-plus']-targets['crest-validation-minus'])*.5;cases=json.loads((ROOT/'crest-validation-cases.json').read_text());target_shape=paired_shape(target_probe,cases);indices=np.concatenate([np.arange(c['probe_start']+24,c['probe_end']-24) for c in cases]);rows=[];best=None;started=time.monotonic()
    for logged in training:
        step=logged['step'];checkpoint=torch.load(ROOT/f'surrogate-{source_version}-step-{step}.pt',weights_only=True);m=core.Model();m.load_state_dict(checkpoint['state_dict']);m.eval()
        assert getattr(m,'continuous_render',False)
        values={name:metrics(targets[name][SR:],core.predict(m,data[name])[SR:])['residual_dbr'] for name in ['music-levels','history-plus','quality-tones']}
        pred=(core.predict(m,data['crest-validation-plus'])-core.predict(m,data['crest-validation-minus']))*.5
        shape=metrics(target_shape,paired_shape(pred,cases));probe=metrics(target_probe[indices],pred[indices]);values['crest-validation-shape-change']=shape['residual_dbr'];score=sum(plan['weights'][k]*v for k,v in values.items());row=dict(step=step,score=score,components=values,crest_probe=probe,crest_shape=shape,seconds=time.monotonic()-started,release_seconds=float(torch.exp(m.release).detach()));rows.append(row)
        if best is None or score<best['score']:
            best=row;core.atomic_write(ROOT/f'surrogate-{version}-continuous-selected.pt',lambda p:torch.save(checkpoint,p))
        path=ROOT/f'selection-{version}-continuous.json';temporary=path.with_suffix('.tmp')
        temporary.write_text(json.dumps(dict(plan=plan,continuous_state=True,all_components_recomputed=True,selected=best,candidates=rows,final_holdouts_used=False),indent=2));temporary.replace(path)
        print(version,step,'score',round(score,4),{k:round(v,3) for k,v in values.items()},flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('version',choices=list(MODULES)+['v18w','v19w']);main(parser.parse_args().version)
