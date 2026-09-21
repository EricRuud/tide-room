"""Freeze the selected refinement before a fresh MDN reference capture."""
import datetime
from p821_900r2_common import *
from p821_900r2_evaluate import candidate,details
from p821_900_capture import material
import p821_measure as bench

def main():
 torch.set_num_threads(1);bench.ROOT=ROOT;manifest=ROOT/'final-freeze.json'
 if not manifest.exists():
  assert not (ROOT/'final-fresh.wav').exists(),'Reference must follow the freeze'
  exported=json.loads((ROOT/'native-export.json').read_text())
  store('final-freeze.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'checkpoint':exported,'source_hashes':{p.name:digest(p) for p in Path(__file__).parent.glob('p821_900r2_*.py')},'seed':903203,'segment_seconds':12,'levels_rms_dbfs':[-28,-22,-16,-10],'new_final_reference_used_before_freeze':False,'old_final_disclosed_as_validation':True})
 freeze=json.loads(manifest.read_text());checkpoint=freeze['checkpoint'];assert digest(ROOT/checkpoint['checkpoint'])==checkpoint['sha256']
 for name,h in checkpoint['files'].items():assert digest(ROOT/'native-model'/name)==h
 for name,h in freeze['source_hashes'].items():assert digest(Path(__file__).parent/name)==h
 pieces=[];segments=[];start=0
 for i,db in enumerate(freeze['levels_rms_dbfs']):
  x=material(freeze['seed']+100*i,seconds=freeze['segment_seconds']);x*=10**(db/20)/np.sqrt(np.mean(x*x));segments.append({'level_dbfs':db,'start':start,'end':start+len(x)});pieces.append(x);start+=len(x)
 source=bench.save('final-fresh',np.concatenate(pieces));bench.submit('final-fresh',source,{'Formula SW':1,'IPS SW':1,'Stage Focus':1})
 d=prepare('final-fresh');old=render(model(),d);new=render(candidate(),d);rows=[]
 for s in segments:
  sl=slice(s['start'],s['end']);rows.append(s|{'parent':details(d['x'][sl],d['y'][sl],old[sl]),'candidate':details(d['x'][sl],d['y'][sl],new[sl])})
 report={'freeze_sha256':digest(manifest),'segments':rows,'overall':{'parent':metrics(d['y'][SR:],old[SR:]),'candidate':metrics(d['y'][SR:],new[SR:])},'claim':'Waveform approximation to the plugin at the specified setting. This is not a hardware match, a null, a perceptual score or proof of zero aliasing.'}
 store('final-evaluation.json',report)
 print(json.dumps({'overall':report['overall'],'segments':[{'level_dbfs':r['level_dbfs'],'parent':r['parent']['overall']['residual_dbr'],'candidate':r['candidate']['overall']['residual_dbr']} for r in rows]},indent=2),flush=True)

if __name__=='__main__':main()
