"""Post-evaluation description of exported residuals; no fitting or selection.

Reads the common-gain PCM24 listening files and restores their documented gain.
These spectra therefore include export quantization. This script was added after
the frozen evaluation; it cannot alter its primary scores or selected models.
"""
import hashlib,json
from pathlib import Path
import numpy as np
import soundfile as sf
from p821_identification_analysis import ROOT,SR,read,linear_kernel,linear_render,metrics
from p821_bandlimited_dictionary import raw_input


def main():
    protocol=json.loads((ROOT/'final-evaluation-protocol.json').read_text());assert protocol['status']=='evaluated'
    folder=Path(protocol['report']).parent;report=json.loads(Path(protocol['report']).read_text());rows=[]
    kernel=linear_kernel();assert hashlib.sha256(kernel.tobytes()).hexdigest()==protocol['linear_kernel_sha256']
    for row in report['rows']:
        waves={}
        for label,entry in row['files'].items():
            path=folder/entry['file'];assert hashlib.sha256(path.read_bytes()).hexdigest()==entry['sha256']
            audio,rate=sf.read(path,always_2d=True);assert rate==SR and audio.shape[1]==2 and np.isfinite(audio).all()
            assert np.max(abs(audio))<=.750001
            waves[label]=audio/10**(row['common_playback_gain_db']/20)
        reference=waves['P821-reference'][SR:];prediction=waves['candidate-native'][SR:];assert reference.shape==prediction.shape
        error=prediction-reference;n=len(error);spectrum=np.fft.rfft(error,axis=0);power=np.sum(abs(spectrum)**2,axis=1)
        weight=np.full(len(power),2.);weight[0]=1.
        if n%2==0:weight[-1]=1.
        energy=power*weight/n;assert np.isclose(energy.sum(),np.sum(error**2),rtol=1e-10)
        freq=np.fft.rfftfreq(n,1/SR);bands=[]
        for a,b in [(0,120),(120,500),(500,2000),(2000,8000),(8000,24000.01)]:
            bands.append(dict(low_hz=a,high_hz=min(24000,b),error_energy_fraction=float(energy[(freq>=a)&(freq<b)].sum()/energy.sum())))
        assert np.isclose(sum(r['error_energy_fraction'] for r in bands),1.)
        peak=np.unravel_index(np.argmax(abs(error)),error.shape)
        source=json.loads((ROOT/(row['recording']+'.json')).read_text())['job']['input']
        baseline=linear_render(raw_input(source),kernel);target=read(row['recording'])
        baseline_score=metrics(target[SR:],baseline[SR:])
        rows.append(dict(recording=row['recording'],bands=bands,quiet_linear_baseline=baseline_score,largest_error_time_seconds=1+peak[0]/SR,
                         largest_error_channel=int(peak[1]),largest_error=float(error[peak]),exported_frames=len(waves['P821-reference']),
                         all_exports_finite_and_peak_limited=True))
        print(row['recording'],'linear baseline',baseline_score['residual_dbr'],[(b['low_hz'],b['high_hz'],round(100*b['error_energy_fraction'],3)) for b in bands],flush=True)
    (folder/'postfreeze-diagnostics.json').write_text(json.dumps(dict(scope=__doc__,no_fitting=True,no_candidate_changes=True,rows=rows),indent=2))


if __name__=='__main__':main()
