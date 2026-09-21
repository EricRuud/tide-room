"""Compare frozen filter bases using target-informed synthetic trajectories.

Uses one existing training case and identical optimization budgets. This is an
audio-path diagnostic, not a predictive emulator or an independent null test.
Only a short input/target segment is read to bound memory use.
"""
import argparse, hashlib, json, tempfile, time
from pathlib import Path
import numpy as np
import soundfile as sf
import torch
from torch import nn
from p821_identification_analysis import ROOT, SR, metrics
from p821_spectral_control_model import Model, prepare_source
from p821_width_separated_model import sample_width
from p821_memory_trajectory import Trajectory


def main(index, steps):
    torch.set_num_threads(1)
    folder = ROOT / 'training-trajectories-v23'
    metadata = json.loads((folder / f'case-{index:02d}.json').read_text())
    labels = np.load(folder / f'case-{index:02d}.npz')
    a, b = metadata['start'], metadata['end']
    source = json.loads((ROOT / 'training-rich.json').read_text())['job']['input']
    with sf.SoundFile(source) as file:
        file.seek(a); raw = file.read(b-a, always_2d=True)
    with tempfile.TemporaryDirectory(prefix='p821-basis-', dir='/private/tmp') as temp:
        path = Path(temp) / 'input.wav'; sf.write(path, raw, SR, subtype='FLOAT')
        d = prepare_source(path)
    with sf.SoundFile(ROOT / 'training-rich.wav') as file:
        file.seek(a); target = file.read(b-a, always_2d=True)
    delta = np.load(ROOT / 'width-labels-training-rich.npz')['focus_delta_db']
    gain = sample_width(delta[a//256:b//256+1], b-a)
    mid = target.mean(axis=1); side = (target[:,0]-target[:,1])*.5/gain
    target = np.column_stack([mid+side, mid-side]) @ np.linalg.inv(d['w']).T
    checkpoint_path = ROOT / 'surrogate-v23-continuous-selected.pt'
    initial = Model(); initial.load_state_dict(torch.load(checkpoint_path, weights_only=True)['state_dict'])
    x = d['base'].T.double(); y = torch.from_numpy(target.T.copy())
    left, right = metadata['case']['start']-a, metadata['case']['end']-a
    iv = torch.from_numpy(labels['values']); ic = torch.from_numpy(labels['cap'])
    energy = torch.mean(y[:,left:right]**2); results = []
    report = dict(target_informed=True, not_emulator_score=True, training_case=index,
                  steps=steps, warm_start='existing 250-step synthetic trajectory',
                  checkpoint_sha256=hashlib.sha256(checkpoint_path.read_bytes()).hexdigest(),
                  final_holdouts_used=False, rows=results)
    destination = ROOT / f'filter-basis-case-{index:02d}.json'
    for variant in ['selected-v23', 'measured-HF', 'learnable-shapes']:
        model = Trajectory(initial, iv, ic)
        if variant == 'measured-HF':
            with torch.no_grad():
                model.filters.frequency[2:] = torch.tensor([2617.5, 9910.]).double().log()
                model.filters.q[2:] = torch.tensor([.789, .521]).double().log()
        for p in model.filters.parameters(): p.requires_grad_(False)
        groups = [dict(params=[model.values, model.cap], lr=.01)]
        if variant == 'learnable-shapes':
            for p in [model.filters.frequency, model.filters.q]: p.requires_grad_(True)
            groups.append(dict(params=[model.filters.frequency, model.filters.q], lr=.0002))
        optimizer = torch.optim.Adam(groups)
        parameters = [p for p in model.parameters() if p.requires_grad]
        initial_cap = model.cap.detach().clone(); history = []; started = time.monotonic()
        for step in range(steps+1):
            prediction = model(x)
            error = torch.mean((prediction[:,left:right]-y[:,left:right])**2)/energy
            if step % 100 == 0 or step == steps:
                row = dict(step=step, seconds=time.monotonic()-started,
                           residual=metrics(y[:,left:right].numpy(), prediction[:,left:right].detach().numpy()),
                           frequencies=model.filters.frequency.exp().detach().tolist(),
                           q=model.filters.q.exp().detach().tolist())
                history.append(row)
                print(variant, step, round(row['residual']['residual_dbr'], 3), flush=True)
            if step == steps: break
            curvature = model.values[:,2:]-2*model.values[:,1:-1]+model.values[:,:-2]
            loss = error+1e-6*(model.values-iv).square().mean()+1e-6*curvature.square().mean()+1e-4*(model.cap-initial_cap).square().mean()
            optimizer.zero_grad(); loss.backward(); nn.utils.clip_grad_norm_(parameters, 1); optimizer.step()
            for group, rate in zip(optimizer.param_groups, [.01, .0002]):
                group['lr'] = rate*(.1+.9*.5*(1+np.cos(np.pi*(step+1)/steps)))
        results.append(dict(variant=variant, history=history))
        destination.write_text(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('--case', type=int, default=5)
    parser.add_argument('--steps', type=int, default=600); args = parser.parse_args()
    main(args.case, args.steps)
