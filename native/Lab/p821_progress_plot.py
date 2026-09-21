"""Development-only model tradeoffs, without opening final recordings."""
import json,os
os.environ.setdefault('MPLCONFIGDIR','/private/tmp/tide-mpl')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from p821_identification_analysis import ROOT


def main():
    plt.rcParams.update({'font.size':10,'axes.spines.top':False,'axes.spines.right':False})
    fig,axes=plt.subplots(1,2,figsize=(12,4.8),layout='constrained')
    colors={'v20':'#828a93','v23':'#4777a8','v25':'#143f63','v26':'#b47641','v27':'#249180','v28':'#8566ad'}
    offsets={'v20':(7,-14),'v23':(-31,-17),'v25':(-34,11),'v26':(7,8)}
    for version in ['v20','v23','v25']:
        selected=json.loads((ROOT/f'selection-{version}-continuous.json').read_text())['selected'];v=selected['components']
        axes[0].scatter(v['music-levels'],v['history-plus'],s=75,c=colors[version],zorder=5)
        axes[0].annotate(version,(v['music-levels'],v['history-plus']),xytext=offsets[version],textcoords='offset points',color=colors[version],weight='bold')
    log=json.loads((ROOT/'surrogate-v26-training.json').read_text());best=min(log,key=lambda r:r['validation']['music-levels']['residual_dbr']);v=best['validation']
    point=(v['music-levels']['residual_dbr'],v['history-plus']['residual_dbr'])
    axes[0].scatter(*point,s=70,c=colors['v26'],marker='x',zorder=5)
    axes[0].annotate('v26 music pick\n(overall rejected)',point,xytext=(8,8),textcoords='offset points',color=colors['v26'],fontsize=9)
    for version in ['v27','v28']:
        path=ROOT/f'surrogate-{version}-training.json'
        if not path.exists():continue
        log=json.loads(path.read_text());music=[r['validation']['music-levels']['residual_dbr'] for r in log];history=[r['validation']['history-plus']['residual_dbr'] for r in log]
        axes[0].plot(music,history,'.-',color=colors[version],alpha=.55,lw=1,markersize=3,label=f'{version} checkpoints')
        axes[1].plot([r['step'] for r in log],music,color=colors[version],label=f'{version} music')
        axes[1].plot([r['step'] for r in log],history,'--',color=colors[version],alpha=.75,label=f'{version} history')
    axes[0].set(xlabel='Music residual (dBr)',ylabel='History residual (dBr)',title='Accuracy trades off across signals',xlim=(-36,-25),ylim=(-36,-19))
    axes[0].text(.02,.04,'Lower and farther left is better',transform=axes[0].transAxes,fontsize=9,color='#555555')
    axes[1].set(xlabel='Training step',ylabel='Residual (dBr)',title='Refinement trajectories — lower is better')
    for ax in axes:ax.grid(alpha=.15);ax.legend(fontsize=8,loc='best')
    fig.suptitle('P821 behavioural model: development validation',fontsize=15,weight='bold')
    fig.text(.01,-.02,'Reused development audio only. Weighted selection also includes tone and crest-shape tests. Final recordings remain unopened.',fontsize=9,color='#555555')
    fig.savefig(ROOT/'overnight-controller-progress.png',dpi=180,bbox_inches='tight');plt.close(fig)


if __name__=='__main__':main()
