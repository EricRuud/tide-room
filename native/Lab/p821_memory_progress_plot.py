"""Development diagnostics for controller history; no final recordings."""
import os,json
os.environ.setdefault('MPLCONFIGDIR','/private/tmp/tide-mpl')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from p821_identification_analysis import ROOT

plt.rcParams.update({'font.size':10,'axes.spines.top':False,'axes.spines.right':False})
fig,axes=plt.subplots(1,2,figsize=(12,5),layout='constrained')
original=json.loads((ROOT/'long-memory-audit-v27-v31.json').read_text())['rows']
rest=json.loads((ROOT/'long-memory-audit-v31rest.json').read_text())['rows']
xs=[r['gap_seconds'] for r in original]
axes[0].plot(xs,[r['reference_history_effect']['residual_dbr'] for r in original],'o-',label='P821 reference',color='#687582')
for version,color in [('v27','#af8251'),('v31','#447ca9')]:axes[0].plot(xs,[r['models'][version]['history_effect']['residual_dbr'] for r in original],'o-',label=version,color=color)
axes[0].plot(xs,[max(-135,r['models']['v31rest']['history_effect']['residual_dbr']) for r in rest],'v-',label='v31 resting-state variant (below chart)',color='#238573')
axes[0].set(xscale='log',xticks=[1,5,15,60],xticklabels=['1','5','15','60'],ylim=(-142,-30),xlabel='Silent gap before the next note (seconds)',ylabel='Effect of the old sound on the new note (dBr)',title='Removing unwanted history after silence')
axes[0].legend(fontsize=8,loc='center');axes[0].grid(alpha=.15)
rows=json.loads((ROOT/'training-context-v27.json').read_text())['rows']
for name,color,label in [('music-levels','#447ca9','Music'),('history-plus','#af8251','Recovery sequence')]:
 group=[r for r in rows if r['dataset']==name];axes[1].plot([r['context_seconds'] for r in group],[r['comparison']['residual_dbr'] for r in group],'o-',label=label,color=color)
axes[1].axvline(.512,color='#777777',ls=':',lw=1);axes[1].annotate('Training warm-up',(.512,-22),xytext=(8,0),textcoords='offset points',fontsize=9)
axes[1].set(xscale='log',xticks=[.064,.128,.256,.512,1.024,2.048],xticklabels=['.064','.128','.256','.512','1.024','2.048'],ylim=(-180,-10),xlabel='Preceding audio supplied to v27 (seconds)',ylabel='Difference from uninterrupted v27 output (dBr)',title='Why training starting state matters')
axes[1].legend(fontsize=9,loc='lower left');axes[1].grid(alpha=.15)
fig.suptitle('Controller memory: diagnosis and correction',fontsize=15,weight='bold')
fig.text(.01,-.04,'Left: one synthetic conditioner/probe pair per gap; reference differences approach repeatability limits. Right: model self-comparison, not P821 accuracy.\nLower is better in both panels. These development diagnostics were prepared before final-recording access and do not establish audibility or perfection.',fontsize=9,color='#555555')
fig.savefig(ROOT/'controller-memory-progress.png',dpi=170,bbox_inches='tight');plt.close(fig)
