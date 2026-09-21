"""Readable final comparison and static scientific plot, with no fitting.

Requires an already completed frozen evaluation. Does not reopen audio or alter
models. The exported WAVs retain one shared playback gain per recording.
"""
import json, os
from pathlib import Path
os.environ.setdefault('MPLCONFIGDIR','/private/tmp/tide-mpl')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from p821_identification_analysis import ROOT


def main():
    protocol=json.loads((ROOT/'final-evaluation-protocol.json').read_text())
    assert protocol['status']=='evaluated' and protocol['final_audio_read']
    report=json.loads(Path(protocol['report']).read_text());folder=Path(protocol['report']).parent
    primary=protocol['chosen'];secondary=protocol.get('secondary_rest_candidate')
    lines=['# P821 overnight comparison','',
      f"The frozen primary candidate is **{primary['version']}, step {primary['step']}**. It was selected using the completed development comparisons before any of these three final recordings was opened.",'',
      'This is an offline model of one captured setting: P821 1.5.0, 456 / 15 ips, Stage Focus Full, Center Off, neutral EQ/bias/gains, optional effects off, 48 kHz and 256-sample reference buffers. The running Tide Room app is unchanged.','']
    if secondary:
        lines += [f"**{secondary['version']}** is the separately selected resting-state option. It was chosen before final access using the documented development-score tolerance and silence-history test. These final scores do not reselect either candidate.",'']
    lines += ['## Previously unused recordings','',
      'Raw stereo waveform residual after the first second; lower is better. No fitted gain, EQ, delay or time warping. A residual is a measurement of difference, not a perceptual rating.','',
      '| Recording | Primary | Resting-state option | Primary active interval |',
      '|---|---:|---:|---:|']
    labels=[];scores=[];rest_scores=[]
    for row in report['rows']:
        p=row['primary'];r=row.get('secondary_rest_candidate',{}).get('diagnostics');value=p['after_first_second']['residual_dbr']
        rest=f"{r['after_first_second']['residual_dbr']:.2f} dBr" if r else 'Same candidate'
        lines += [f"| {row['recording']} | {value:.2f} dBr | {rest} | {p['active']['residual_dbr']:.2f} dBr |"]
        labels.append(row['recording'].removeprefix('test-').replace('-',' '));scores.append(value);rest_scores.append(r['after_first_second']['residual_dbr'] if r else value)
    lines += ['','## Listen','',
      'Every version of a recording uses the same gain, attenuated by at least 12 dB. Start at a comfortable playback volume. The HQ mode oversamples the final limiter; it does not oversample every stage. When the limiter is inactive, an HQ label can refer to the same WAV.','']
    for row in report['rows']:
        lines += [f"### {row['recording']}",'',f"Shared playback gain: {row['common_playback_gain_db']:.2f} dB.",'']
        for label,entry in row['files'].items():
            alias=f" (identical to {entry['identical_to']})" if 'identical_to' in entry else ''
            lines.append(f"- [{label}]({entry['file']}){alias}")
        lines.append('')
    lines += ['## What the checks establish','',
      '- The candidate runs from the input alone, carrying its state through each whole recording. Reference audio and fitted reference trajectories do not enter inference.',
      '- Native C++ and Python agreement is a port check, separate from agreement with P821. See the native report for callback, reset and oversampling evidence.',
      '- The resting-state correction removes persistent learned history in the tested conditioner/silence/note sequence. Those probes motivated the correction and are development diagnostics.',
      '- DC response and recovery-shape mismatches remain. No perfect null, inaudibility, subjective listening verdict, zero-aliasing or real-time device guarantee is claimed.','',
      'The raw report includes separate channels, mid/side, 250 ms windows, input-selected transients, high-frequency residuals, peaks and HQ comparisons. High-frequency residual is not an isolated aliasing measurement.','',
      '[Raw final measurements](report.json) · [Native implementation and checks](../NATIVE-MODEL.md) · [Detailed dynamics research](../P821-DYNAMICS.md) · [Sources](../RESEARCH.md)','']
    (folder/'README.md').write_text('\n'.join(lines))
    plt.rcParams.update({'font.size':11,'axes.spines.top':False,'axes.spines.right':False})
    fig,ax=plt.subplots(figsize=(9,4.8),layout='constrained');positions=np.arange(len(labels));width=.34
    ax.barh(positions-width/2,scores,height=width,label=primary['version'],color='#4d7f9d')
    if secondary:ax.barh(positions+width/2,rest_scores,height=width,label=secondary['version'],color='#3c907e')
    for ys,values in [(positions-width/2,scores),(positions+width/2,rest_scores)] if secondary else [(positions-width/2,scores)]:
        for y,v in zip(ys,values):ax.text(v-.5,y,f'{v:.2f}',ha='right',va='center',fontsize=10)
    ax.set(yticks=positions,yticklabels=labels,xlabel='Raw residual versus P821 (dBr; farther left is better)',xlim=(min(scores+rest_scores)-8,0),title='Frozen candidates on previously unused recordings')
    ax.invert_yaxis();ax.legend(loc='lower right');ax.grid(axis='x',alpha=.15);ax.set_axisbelow(True)
    fig.text(.01,-.04,'No gain/EQ/delay fit. One fixed setting at 48 kHz / 256 samples. This is waveform agreement, not a listening score.',fontsize=9,color='#555555')
    fig.savefig(folder/'final-comparison.png',dpi=170,bbox_inches='tight');plt.close(fig)
    print(folder/'README.md')


if __name__=='__main__':main()
