"""Private comparison renders of a selected candidate and the installed reference.

Uses existing development music, not final holdouts. Every variant in a passage
gets the same gain. No playback is initiated and no subjective verdict is made.
"""
import argparse,hashlib,importlib,json
import numpy as np
import soundfile as sf
import torch
from p821_identification_analysis import ROOT,SR,read,metrics
from p821_select_continuous import MODULES
from p821_stateful_render import parts,mix
from p821_memory_limiter import forward as ordinary
from p821_multistage_limiter import render as continuous


def main(version):
    extension=importlib.import_module(MODULES[version]);extension.activate();core=extension.core;torch.set_num_threads(1)
    checkpoint_path=ROOT/f'surrogate-{version}-continuous-selected.pt';checkpoint=torch.load(checkpoint_path,weights_only=True);m=core.Model();m.load_state_dict(checkpoint['state_dict']);m.eval()
    d=core.prepare_source(json.loads((ROOT/'music-levels.json').read_text())['job']['input']);x,cap=parts(m,d);tau=float(torch.exp(m.release).detach())
    reference=read('music-levels');limited,states,_=ordinary(x.T.copy(),cap.T.copy(),np.exp(-1/(SR*tau)),np.zeros(2))
    activity=dict(maximum_pre_ceiling_ratio=float(np.max(abs(x)/np.maximum(cap,1e-9))),limited_sample_fraction=float(np.mean(states>1)))
    native=mix(limited.T,d);hq=mix(continuous(x,cap,tau,8,span=256),d)
    folder=ROOT/f'listen-{version}';folder.mkdir(exist_ok=True);rows=[]
    for case in json.loads((ROOT/'input-music-levels.json').read_text()):
        a,b=case['start'],case['end'];label=f"drive-{case['gain_db']:02d}";variants={'P821-reference':reference[a:b],'candidate-native':native[a:b],'candidate-HQ':hq[a:b]}
        maximum=max(float(np.max(abs(y))) for y in variants.values());gain=min(10**(-12/20),.75/max(maximum,1e-12));files={}
        for name,y in variants.items():
            if name=='candidate-HQ' and np.array_equal(y,variants['candidate-native']):
                files[name]=dict(files['candidate-native'],identical_to='candidate-native');continue
            path=folder/f'{label}-{name}.wav';sf.write(path,y*gain,SR,subtype='PCM_24');files[name]=dict(file=path.name,peak=float(np.max(abs(y*gain))),sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        rows.append(case|dict(common_playback_gain_db=float(20*np.log10(gain)),files=files,native_raw_residual=metrics(reference[a:b],native[a:b]),hq_raw_residual=metrics(reference[a:b],hq[a:b]),hq_vs_native=metrics(native[a:b],hq[a:b])))
    report=dict(version=version,checkpoint_step=checkpoint['step'],checkpoint_sha256=hashlib.sha256(checkpoint_path.read_bytes()).hexdigest(),sample_rate=SR,reference_block_size=256,development_audio=True,final_holdouts_used=False,individual_loudness_matching=False,limiter_activity=activity,HQ=dict(method='quadratic continuous envelope with compensated integration and cascaded FIRs',output_factor=8,envelope_factor=16,FIR_span=256,not_zero_aliasing=True),rows=rows)
    (folder/'comparison.json').write_text(json.dumps(report,indent=2))
    text=f'''# P821 comparison — {version}, step {checkpoint['step']}

These are the same development recording at three input drive levels. Each level
has the installed P821 reference, the candidate's native-rate renderer, and its
experimental higher-quality limiter renderer. All three receive the same playback
gain (at least 12 dB attenuation), with no individual gain/EQ matching. Processing
runs continuously across the original recording before the passages are cut.

The 00 files are quiet, 12 files moderately driven, and 18 files strongly driven.
Compare files with the same drive number. The original input is a generative room
recording already used for development; these are not final holdout results.

This is work in progress. The candidate is not installed in Tide Room, has not
achieved a deep predictive null, and has not received a subjective listening
evaluation here. HQ reduces measured limiter sampling artifacts; it does not
guarantee zero aliasing and is not expected to null exactly with P821's renderer.
The whole emulation is currently fixed to the captured settings and 48 kHz.
Limiter activity in this recording: {activity['limited_sample_fraction']:.6%} of
channel samples; maximum pre-limiter/ceiling ratio {activity['maximum_pre_ceiling_ratio']:.6f}.
Identical HQ/native audio shares one WAV file, as recorded in comparison.json.
When the limiter stays inactive these passages cannot demonstrate its quality
option; they compare the moving EQ/gain/width model with P821.

Exact settings, common gain, hashes and unadjusted residuals are in comparison.json.
'''
    (folder/'README.md').write_text(text);print('Saved',folder,'without starting playback',flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('version',choices=list(MODULES));main(parser.parse_args().version)
