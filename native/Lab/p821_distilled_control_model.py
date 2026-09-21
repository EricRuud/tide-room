"""v26: predict synthetic training trajectories with an input-only controller.

Latent labels are target-informed, non-unique optimization aids. They are used
only in training. Validation uses complete raw output waveforms, and inference
loads neither target audio nor latent labels. Filter shapes, ceiling growth and
limiter release remain frozen to the teacher's v23 audio path.
"""
import argparse, hashlib, json, time
import numpy as np
import torch
from torch import nn
import p821_crest_gate_model as previous
from p821_identification_analysis import ROOT, SR, metrics

Model = previous.Model
prepare_source = previous.prepare_source
core = previous.core


def activate():
    previous.activate(); core.VERSION = 'v26'


def load_labels():
    folder = ROOT / 'training-trajectories-v23'
    plan = json.loads((folder / 'plan.json').read_text())
    checkpoint = ROOT / 'surrogate-v23-continuous-selected.pt'
    assert hashlib.sha256(checkpoint.read_bytes()).hexdigest() == plan['checkpoint_sha256']
    data = []
    for index in plan['case_indices']:
        path = folder / f'case-{index:02d}.npz'
        assert path.exists() and path.with_suffix('.json').exists(), f'Trajectory {index} incomplete'
        z = np.load(path)
        # An inactive ceiling is unobservable. In particular the trajectory
        # fitter's [0.65, 3] bounds must not become arbitrary ceiling labels.
        moved = abs(np.log(z['cap']/np.maximum(z['initial_cap'],.01))) > .005
        identifiable = moved & (z['cap'] > .655) & (z['cap'] < 2.995)
        target_cap = np.where(identifiable,z['cap'],z['initial_cap'])
        data.append(dict(control=torch.from_numpy(z['control'].transpose(1,0,2).copy()),
                         values=torch.from_numpy(z['values']),
                         cap=torch.from_numpy(target_cap), mask=torch.from_numpy(z['mask']),
                         initial_values=torch.from_numpy(z['initial_values']),
                         index=index, sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    return data, plan


def latent_batch_loss(model, training, rng, step):
    length, context = 192, 128
    examples = []
    for j in range(8):
        d = training[(step*8+j) % len(training)]
        a = int(rng.integers(0,d['control'].shape[1]-length+1)); c = int(rng.integers(0,2))
        examples.append((d,a,c))
    controls = torch.stack([d['control'][c,a:a+length] for d,a,c in examples])
    target_values = torch.stack([d['values'][c,a+context:a+length] for d,a,c in examples])
    target_cap = torch.stack([d['cap'][c,a+context:a+length] for d,a,c in examples])
    initial = torch.stack([d['initial_values'][c,a+context:a+length] for d,a,c in examples])
    mask = torch.stack([d['mask'][a+context:a+length] for d,a,c in examples]).double()
    values, cap = model.control_parts(controls)
    values, cap = values[:,context:], cap[:,context:]
    difference = (values-target_values).square().mean(dim=2)
    ceiling_difference = (20*torch.log10(cap.clamp_min(.01)/target_cap)).square()
    anchor = (values-initial).square().mean(dim=2)
    return torch.sum(mask*(difference+.2*ceiling_difference+.01*anchor))/mask.sum().clamp_min(1)


def main(steps):
    activate(); torch.set_num_threads(2); torch.manual_seed(25821); rng = np.random.default_rng(25821)
    training, teacher_plan = load_labels()
    plan = json.loads((ROOT / 'selection-plan-v25.json').read_text())
    plan.update(candidate_family='v26', expected_final_step=steps,
                candidate_steps=f'All completed 500-step checkpoints, 0 through {steps}',
                structural_change='Same v25 controller, direct synthetic latent trajectory training; v23 shapes/release frozen',
                training_labels=[dict(case=d['index'], sha256=d['sha256']) for d in training],
                teacher_plan=teacher_plan, inference_target_free=True, final_holdouts_used=False,
                latent_loss='MSE in five smoothed dB controls plus ceiling dB, with 0.01 anchor to initial values',
                ceiling_label_rule='Use moved interior teacher ceilings only; anchor inactive/boundary ceilings to initial v23; this is a heuristic, not proof of identifiability')
    (ROOT / 'selection-plan-v26.json').write_text(json.dumps(plan, indent=2))
    validation = [core.validation_dataset(n) for n in ['music-levels','history-plus','quality-tones','detector-pulses']]
    model = Model(); previous.initialize(model)
    for name, parameter in model.named_parameters():
        parameter.requires_grad_(name.startswith(('controller.', 'output.', 'memory_output.', 'crest_output.')) or name == 'tau')
    parameters = [p for p in model.parameters() if p.requires_grad]
    optimizer = torch.optim.Adam(parameters, lr=.0002)
    log = []; started = time.monotonic()
    for step in range(steps+1):
        if step % 500 == 0:
            model.eval()
            row = dict(step=step, seconds=time.monotonic()-started,
                       validation={d['name']:metrics(d['y'][SR:],core.predict(model,d)[SR:]) for d in validation},
                       frequencies=model.frequency.exp().detach().tolist(),q=model.q.exp().detach().tolist(),tau=model.tau.exp().detach().tolist())
            log.append(row); (ROOT / 'surrogate-v26-training.json').write_text(json.dumps(log, indent=2))
            checkpoint = dict(state_dict=model.state_dict(),step=step)
            torch.save(checkpoint, ROOT / f'surrogate-v26-step-{step}.pt')
            torch.save(checkpoint, ROOT / 'surrogate-v26.pt')
            print(step,{k:round(v['residual_dbr'],2) for k,v in row['validation'].items()},'seconds',round(time.monotonic()-started,1),flush=True)
            model.train()
        if step == steps: break
        loss = latent_batch_loss(model,training,rng,step)
        optimizer.zero_grad(); loss.backward(); nn.utils.clip_grad_norm_(parameters, 1); optimizer.step()
        for group in optimizer.param_groups:
            group['lr'] = .0002*(.05+.95*.5*(1+np.cos(np.pi*(step+1)/steps)))
        if step % 100 == 99:
            print('step',step+1,'latent loss',float(loss.detach()),'seconds',round(time.monotonic()-started,1),flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('--steps',type=int,default=10000)
    main(parser.parse_args().steps)
