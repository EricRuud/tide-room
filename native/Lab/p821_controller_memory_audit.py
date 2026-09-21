"""Input-only analysis of learned controller persistence during silence."""
import importlib,json
import numpy as np
import torch
from p821_identification_analysis import ROOT,SR
from p821_select_continuous import MODULES


def main():
    torch.set_num_threads(1);rows=[];seconds=[0,.064,.128,.256,.512,1.024,2.048,5.,10.,30.,60.]
    for version in ['v27','v31']:
        extension=importlib.import_module(MODULES[version]);model=extension.Model();model.load_state_dict(torch.load(ROOT/f'surrogate-{version}-continuous-selected.pt',weights_only=True)['state_dict']);model.eval()
        quiet=torch.full((1,1,61),-3.);quiet[:,:,14]=-2.
        with torch.no_grad():
            sequence=quiet.repeat(1,int(60*SR/256)+1,1);trajectory,_=model.controller(sequence)
            settled=trajectory[:,-1].clone();initial=trajectory[:,0];equilibrium_change=float(torch.max(abs(trajectory[:,-1]-trajectory[:,-2])))
            step=lambda h:model.controller(quiet,h.reshape(1,1,48))[1].reshape(48)
        jacobian=torch.autograd.functional.jacobian(step,settled.reshape(48).requires_grad_(True)).detach().numpy()
        radius=float(np.max(abs(np.linalg.eigvals(jacobian))));tau=-256/SR/np.log(radius) if 0<radius<1 else None
        with torch.no_grad():
            driven=quiet.clone();driven[:,:,:]=.5;driven[:,:,14]=.2
            _,excited=model.controller(driven.repeat(1,20,1),settled.reshape(1,1,48));recovery,_=model.controller(sequence,excited)
            distance=torch.linalg.vector_norm(recovery-settled[:,None,:],dim=2)[0].numpy()
            quiet_rows=[dict(seconds=s,from_zero_distance=float(torch.linalg.vector_norm(trajectory[:,round(s*SR/256)]-settled)),after_drive_distance=float(distance[round(s*SR/256)])) for s in seconds]
        row=dict(version=version,quiet_endpoint_last_step_change=equilibrium_change,local_jacobian_spectral_radius=radius,endpoint_linearization_tau_seconds=tau,endpoint_seconds=60,equilibrium_not_established=True,
                 interpretation='Finite-endpoint hidden-state diagnostic, not a proven fixed point, global decay rate, audio release time or physical tape constant',trajectory=quiet_rows)
        rows.append(row);print(json.dumps(row),flush=True)
    (ROOT/'controller-memory-audit.json').write_text(json.dumps(dict(final_holdouts_used=False,reference_audio_used=False,rows=rows),indent=2))


if __name__=='__main__':main()
