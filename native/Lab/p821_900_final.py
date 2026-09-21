"""One final, separately seeded test after the checkpoint is frozen."""
import hashlib,datetime,subprocess,tempfile
from p821_900_model import *
import p821_900_capture as capture

def main():
 torch.set_num_threads(1);manifest=ROOT/'final-freeze.json'
 if not manifest.exists():
  assert not (ROOT/'final-new-material.wav').exists(),'Reference predates freeze'
  exported=json.loads((ROOT/'native-export.json').read_text());chosen=ROOT/exported['checkpoint']
  row={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'checkpoint':exported,'source_hashes':{p.name:digest(p) for p in Path(__file__).parent.glob('p821_900_*.py')},'seed':903003,'levels_rms_dbfs':[-28,-22,-16,-10],'normal_range_rms_dbfs':[-28,-22,-16],'final_reference_used_before_freeze':False}
  store('final-freeze.json',row)
 freeze=json.loads(manifest.read_text());checkpoint=freeze['checkpoint'];assert digest(ROOT/checkpoint['checkpoint'])==checkpoint['sha256']
 for name,h in checkpoint['files'].items():assert digest(ROOT/'native-model'/name)==h
 pieces=[];segments=[];a=0
 for i,db in enumerate(freeze['levels_rms_dbfs']):
  x=capture.material(freeze['seed']+100*i,seconds=16);x*=10**(db/20)/np.sqrt(np.mean(x*x));segments.append({'level_dbfs':db,'start':a,'end':a+len(x)});pieces.append(x);a+=len(x)
 x=np.concatenate(pieces);source=capture.save('final-new-material',x);capture.submit('final-new-material',source)
 d=prepare('final-new-material');m=RestModel();m.load_state_dict(torch.load(ROOT/checkpoint['checkpoint'],weights_only=True)['state_dict']);m.eval();prediction=render(m,d)
 rows=[]
 for s in segments:
  sl=slice(s['start']+SR,s['end']);rows.append(s|{'metrics':metrics(d['y'][sl],prediction[sl])})
 report={'freeze_sha256':digest(manifest),'overall':metrics(d['y'][SR:],prediction[SR:]),'segments':rows,'claim':'Behavioral approximation; this test does not establish a null, hardware equivalence, perceptual equivalence, or zero aliasing.'};store('final-evaluation.json',report);print(json.dumps(report,indent=2),flush=True)
if __name__=='__main__':main()
