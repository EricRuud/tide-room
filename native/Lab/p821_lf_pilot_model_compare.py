"""Post-freeze mechanism diagnostic on exploratory probes, not final selection.

The carrier at bin 8 can couple pilot bin 7 to bin 9 and vice versa.
Record both the direct complex response and conjugate-input mirror response.
"""
import json, subprocess, tempfile
from pathlib import Path
import numpy as np
import soundfile as sf
from p821_lf_experiments import ROOT, OLD, SR, N, digest, save_json
from p821_identification_analysis import metrics


def response(x,y,k):
    a=11*N;b=14*N;X=np.fft.rfft(x[a:b,0]);Y=np.fft.rfft(y[a:b,0]);mirror=16-k
    direct=Y[3*k]/X[3*k];coupled=Y[3*mirror]/np.conj(X[3*k])
    complex_value=lambda z:dict(real=float(z.real),imag=float(z.imag),gain_db=float(20*np.log10(max(abs(z),1e-15))),phase_deg=float(np.angle(z,deg=True)))
    return dict(direct=complex_value(direct),mirror_conjugate=complex_value(coupled),mirror_frequency=mirror*SR/N)


def main():
    protocol=json.loads((ROOT/'final-lf-protocol.json').read_text());assert protocol['status']=='evaluated'
    binary=Path(protocol['binaries']['native']);assert digest(binary)==protocol['hashes'][str(binary)]
    cases=json.loads((ROOT/'pilot-convergence-cases.json').read_text());rows=[]
    with tempfile.TemporaryDirectory(prefix='p821-lf-pilot-model-',dir='/private/tmp') as temp:
        temp=Path(temp)
        for case in cases:
            if case['pilot_amplitude'] not in [.0024,.0048]:continue
            name=case['name'];k=case['pilot_bin'];inputs=[];targets=[]
            for label in ['plus','minus']:
                x,sr=sf.read(ROOT/f'input-{name}-{label}.wav',always_2d=True);assert sr==SR;inputs.append(x)
                y,sr=sf.read(ROOT/f'{name}-{label}.wav',always_2d=True);assert sr==SR;targets.append(y)
            marginal=.5*(targets[0]-targets[1]);pilot=.5*(inputs[0]-inputs[1]);row=case|dict(reference=response(pilot,marginal,k),models={})
            for version,config in [('parent',OLD/'native-model-v31rest'),('candidate',ROOT/'native-lf-gated')]:
                outputs=[]
                for x in inputs:
                    x.astype('<f4').tofile(temp/'input.bin')
                    subprocess.check_output([str(binary),str(config),str(temp/'input.bin'),str(temp/'output.bin')],text=True)
                    y=np.fromfile(temp/'output.bin',dtype='<f8').reshape(-1,2);assert np.isfinite(y).all();outputs.append(y)
                predicted=.5*(outputs[0]-outputs[1]);row['models'][version]=dict(
                  response=response(pilot,predicted,k),plateau=metrics(marginal[11*N:14*N],predicted[11*N:14*N]),
                  whole_after_first_second=metrics(marginal[SR:],predicted[SR:]))
            rows.append(row)
            print(name,'marginal plateau residual',[(k,v['plateau']['residual_dbr']) for k,v in row['models'].items()],flush=True)
    save_json(ROOT/'pilot-model-comparison.json',dict(rows=rows,candidate_checkpoint_sha256=protocol['checkpoint_sha256'],
      binary_sha256=digest(binary),script_sha256=digest(__file__),post_freeze=True,final_used_for_fitting=False,no_model_changes=True,
      interpretation='Specified periodic carrier and finite pilot amplitudes. Direct and conjugate mirror responses are parts of a local frequency-coupling response, not a globally valid LTI filter.'))


if __name__=='__main__':main()
