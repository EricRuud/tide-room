"""Summarize the exploratory coupling map; fit diagnostic lines, no DSP model.

A single memoryless scalar nonlinearity between fixed LTI pre/post filters,
under a settled sinusoidal carrier, has a fixed phase (up to sign) for a given
conjugate mirror response as carrier amplitude changes. A best real line in
the complex plane allows arbitrary amplitude/sign changes. Departures motivate
testing richer structures; they do not uniquely identify memory, feedback, or
P821 internals. Multiple parallel static branches can also produce phase changes.
"""
import json
import numpy as np
from p821_lf_experiments import ROOT, SR, N, save_json, digest


def complex_response(row,which='reference'):
    r=row['reference'] if which=='reference' else row['models'][which]['response']
    h=r['mirror_conjugate'];return h['real']+1j*h['imag']


def main():
    report=json.loads((ROOT/'coupling-map-analysis.json').read_text());assert report['complete'];rows=report['rows'];groups=[]
    for carrier in [6,8,10,12]:
        for offset in [-1,1]:
            cases=sorted([r for r in rows if r['carrier_bin']==carrier and r['pilot_bin']==carrier+offset],key=lambda r:r['carrier_peak']);assert len(cases)==3
            z=np.array([complex_response(r) for r in cases]);matrix=np.column_stack([z.real,z.imag]);_,s,vh=np.linalg.svd(matrix,full_matrices=False)
            estimate=(matrix@vh[0])[:,None]*vh[0][None,:];error=np.linalg.norm(matrix-estimate)/max(np.linalg.norm(matrix),1e-30)
            phase=np.degrees(np.unwrap(2*np.angle(z))/2)
            period_spread=[]
            for r in cases:
                zz=np.array([p['mirror_conjugate']['real']+1j*p['mirror_conjugate']['imag'] for p in r['reference_periods']])
                period_spread.append(float(np.ptp(np.degrees(np.unwrap(np.angle(zz))))))
            groups.append(dict(carrier_bin=carrier,carrier_frequency_hz=carrier*SR/N,pilot_frequency_hz=(carrier+offset)*SR/N,
              mirror_frequency_hz=(carrier-offset)*SR/N,carrier_peaks=[r['carrier_peak'] for r in cases],
              mirror_gain_db=[r['reference']['mirror_conjugate']['gain_db'] for r in cases],phase_modulo_sign_deg=phase.tolist(),
              phase_range_modulo_sign_deg=float(np.ptp(phase)),late_window_phase_spread_deg=period_spread,
              best_fixed_phase_residual_dbr=float(20*np.log10(max(error,1e-15))),singular_values=s.tolist()))
    improvements=[r['models']['parent']['plateau']['residual_dbr']-r['models']['candidate']['plateau']['residual_dbr'] for r in rows]
    save_json(ROOT/'coupling-map-summary.json',dict(groups=groups,
      candidate_vs_parent=dict(improved_count=sum(v>0 for v in improvements),case_count=len(rows),mean_improvement_db=float(np.mean(improvements)),minimum_improvement_db=float(min(improvements)),maximum_improvement_db=float(max(improvements))),
      spot_checks=[{k:r[k] for k in ['name','half_amplitude_convergence','repeat_noise_relative_to_marginal','half_amplitude_response','repeat_line_error']} for r in rows if r['spot_check']],
      script_sha256=digest(__file__),input_report_sha256=digest(ROOT/'coupling-map-analysis.json'),
      post_final_exploration=True,no_runtime_fitting=True,not_a_holdout_score=True,
      limitation='Fixed-phase comparison is a local diagnostic for one scalar memoryless nonlinearity between fixed LTI filters and a settled sine carrier. Drive-dependent phase can also arise from mixtures of static nonlinear branches, not only memory or feedback.'))
    for g in groups:print(g['carrier_frequency_hz'],g['pilot_frequency_hz'],'phase range',g['phase_range_modulo_sign_deg'],'fixed phase error',g['best_fixed_phase_residual_dbr'],'within-window phase spreads',g['late_window_phase_spread_deg'],flush=True)
    plot(rows)


def plot(rows):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig,axes=plt.subplots(2,2,figsize=(11,8),layout='constrained')
    for ax,carrier in zip(axes.flat,[6,8,10,12]):
        for offset,color in [(-1,'#167b88'),(1,'#c15c31')]:
            cases=sorted([r for r in rows if r['carrier_bin']==carrier and r['pilot_bin']==carrier+offset],key=lambda r:r['carrier_peak'])
            for which,style in [('reference','o-'),('candidate','x--')]:
                z=np.array([complex_response(r,which) for r in cases])
                # Use a shared drive reference and a bounded phase convention;
                # accumulated unwrapping can choose different 180-degree branches.
                phase=np.degrees(np.angle((z/z[1])**2)/2)
                ax.plot([r['carrier_peak'] for r in cases],phase,style,color=color,label=f'{which}: pilot {(carrier+offset)*SR/N:.1f} Hz')
        ax.set(title=f'Carrier {carrier*SR/N:.2f} Hz',xlabel='Carrier peak amplitude',ylabel='Mirror phase change from .4 peak (degrees)',ylim=(-95,95))
        ax.axvspan(.13,.17,color='gray',alpha=.12)
        ax.grid(alpha=.2);ax.legend(fontsize=8)
    fig.suptitle('Exploratory mirror phase, relative to .4 peak, modulo 180 degrees\nFinite .0024 pilots; shaded lower drive includes poor amplitude convergence',fontsize=12)
    fig.savefig(ROOT/'coupling-map-phase.png',dpi=150);plt.close(fig)


if __name__=='__main__':main()
