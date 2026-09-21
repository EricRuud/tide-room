"""Select a frozen-cost refinement using the existing five validation fixtures.

Three parameter averages can balance the two nearby fine-tunes. This is offline
weight averaging, not two simultaneous audio engines. No final reference is used.
"""
import shutil
from p821_900r2_common import *
from p821_900r2_train import NAMES,WEIGHTS

def main():
 torch.set_num_threads(1);assert not (ROOT/'final-freeze.json').exists()
 original=ROOT/'attack-candidate-best.pt'
 if not original.exists():shutil.copyfile(ROOT/'candidate-best.pt',original)
 if not (ROOT/'attack-candidate-best.json').exists():shutil.copyfile(ROOT/'candidate-best.json',ROOT/'attack-candidate-best.json')
 a=torch.load(original,weights_only=True);b=torch.load(ROOT/'recovery-candidate-best.pt',weights_only=True)
 data=[prepare(n) for n in NAMES];baseline=json.loads((PARENT/'tonal-best.json').read_text())['validation']
 rows=[];chosen=None;best=sum(baseline[n]['residual_dbr']*w for n,w in zip(NAMES,WEIGHTS))
 store('selection-plan.json',{'candidates':'Attack best, recovery best, and .25/.5/.75 parameter averages toward recovery','source_hashes':{'attack':digest(original),'recovery':digest(ROOT/'recovery-candidate-best.pt')},'same_validation_weights_and_guards':True,'new_final_used':False})
 for fraction in [0.,.25,.5,.75,1.]:
  state={k:a['state_dict'][k]*(1-fraction)+b['state_dict'][k]*fraction for k in a['state_dict']}
  m=RestModel();m.load_state_dict(state);m.eval();scores={d['name']:metrics(d['y'][SR:],render(m,d)[SR:]) for d in data}
  score=sum(scores[n]['residual_dbr']*w for n,w in zip(NAMES,WEIGHTS))
  admissible=all(scores[n]['residual_dbr']<=baseline[n]['residual_dbr']+(.25 if n=='validation-room' else .75) for n in NAMES)
  row={'recovery_fraction':fraction,'score':score,'admissible':admissible,'validation':scores};rows.append(row);print(fraction,score,{k:round(v['residual_dbr'],3) for k,v in scores.items()},flush=True)
  if admissible and score<best:chosen={'state_dict':state,'step':b['step'] if fraction==1 else a['step'] if fraction==0 else -1,'recovery_fraction':fraction};best=score
 assert chosen is not None,'No acceptable improvement';torch.save(chosen,ROOT/'candidate-best.pt')
 store('candidate-best.json',next(r for r in rows if r['recovery_fraction']==chosen['recovery_fraction'])|{'method':'Offline parameter average of the retained attack and recovery checkpoints','step':chosen['step']})
 store('selection.json',{'rows':rows,'chosen_recovery_fraction':chosen['recovery_fraction'],'chosen_score':best,'chosen_sha256':digest(ROOT/'candidate-best.pt')})

if __name__=='__main__':main()
