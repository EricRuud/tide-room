"""Separate input-predicted width improvements from target-informed ceilings."""
import json
import numpy as np
import torch
from p821_identification_analysis import ROOT,SR,read,metrics
from p821_peak_control_model import prepare_source
from p821_width_separated_model import prepare_source as width_prepare,sample_width
from p821_stateful_render import predict


def ms(y):return np.column_stack([y.mean(axis=1),(y[:,0]-y[:,1])*.5])


def main():
    torch.set_num_threads(2);source=json.loads((ROOT/'music-levels.json').read_text())['job']['input'];d=prepare_source(source);dw=width_prepare(source);target=read('music-levels');cases=json.loads((ROOT/'input-music-levels.json').read_text());report={}
    delta=np.load(ROOT/'width-labels-music-levels.npz')['focus_delta_db'];oracle={**d,'extra_width_gain':sample_width(delta,len(d['x']))}
    for version in ['v18','v19']:
        if version=='v18':from p821_memory_control_model import Model
        else:from p821_history_gate_model import Model
        checkpoint=torch.load(ROOT/f'surrogate-{version}-selected.pt',weights_only=True);m=Model();m.load_state_dict(checkpoint['state_dict']);m.eval()
        outputs={'original':predict(m,d),'input_width':predict(m,dw),'reference_width_diagnostic':predict(m,oracle)};rows=[]
        for c in cases:
            a,b=c['start'],c['end'];row={**c,'variants':{}}
            for name,y in outputs.items():
                row['variants'][name]=dict(stereo=metrics(target[a:b],y[a:b]),mid=metrics(ms(target[a:b])[:,0],ms(y[a:b])[:,0]),side=metrics(ms(target[a:b])[:,1],ms(y[a:b])[:,1]))
            rows.append(row);print(version,c['gain_db'],{k:round(v['stereo']['residual_dbr'],3) for k,v in row['variants'].items()},flush=True)
        report[version]=dict(step=checkpoint['step'],passages=rows)
    (ROOT/'width-music-audit.json').write_text(json.dumps(dict(final_holdouts_used=False,reference_width_is_target_informed=True,results=report),indent=2))
    import matplotlib;matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    t=np.arange(len(dw['extra_width_gain']))/SR;fig,axes=plt.subplots(2,1,figsize=(11,6),sharex=True)
    axes[0].plot(t[::64],20*np.log10(oracle['extra_width_gain'][::64]),label='Reference-pair measurement',lw=1)
    axes[0].plot(t[::64],20*np.log10(dw['extra_width_gain'][::64]),label='Input-only prediction',lw=1,alpha=.8);axes[0].set_ylabel('Extra width (dB)');axes[0].legend()
    axes[1].plot(t[::64],d['x'][::64,0],lw=.5);axes[1].set_ylabel('Source left');axes[1].set_xlabel('Seconds');fig.suptitle('Separate width controller: ongoing music validation, not final holdouts');fig.tight_layout();fig.savefig(ROOT/'width-music-audit.png',dpi=150)


if __name__=='__main__':main()
