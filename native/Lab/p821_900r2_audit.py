"""Verify preserved experiments and the exact coefficients in the audition app."""
import argparse
from p821_900r2_common import *

def main(resources):
 entries={}
 def collect(node):
  if isinstance(node,dict):
   for k,v in node.items():
    if k.startswith('/') and isinstance(v,str) and len(v)==64:entries[k]=v
    else:collect(v)
  elif isinstance(node,list):
   for v in node:collect(v)
 collect(json.loads((PARENT.parent/'p821-identification-002/final-lf-protocol.json').read_text()))
 assert len(entries)>=207,len(entries)
 for path,h in entries.items():assert digest(path)==h,path
 first=json.loads((PARENT/'final-freeze.json').read_text());assert digest(PARENT/first['checkpoint']['checkpoint'])==first['checkpoint']['sha256']
 for name,h in first['source_hashes'].items():assert digest(Path(__file__).parent/name)==h,name
 for name,h in first['checkpoint']['files'].items():assert digest(PARENT/'native-model'/name)==h,name
 new=json.loads((ROOT/'final-freeze.json').read_text());assert digest(ROOT/new['checkpoint']['checkpoint'])==new['checkpoint']['sha256']
 for name,h in new['source_hashes'].items():assert digest(Path(__file__).parent/name)==h,name
 for name,h in new['checkpoint']['files'].items():assert digest(ROOT/'native-model'/name)==h,name
 bundled={}
 if resources:
  folder=Path(resources)
  for name,source in [('model',PARENT.parent/'p821-identification-002/native-lf-gated'),('90030',ROOT/'native-model'),('hq',PARENT.parent/'p821-identification-001/native-hq-limiter')]:
   files=list(source.glob('*.bin'));assert {p.name for p in files}=={p.name for p in (folder/name).glob('*.bin')}
   for p in files:assert digest(folder/name/p.name)==digest(p),str(folder/name/p.name)
   bundled[name]=len(files)
 report={'456_frozen_entries_unchanged':len(entries),'first_900_checkpoint_sources_and_coefficients_unchanged':True,'selected_900_freeze_matches':True,'bundle_verified':str(resources) if resources else None,'bundled_files':bundled}
 store('preservation-audit.json',report);print(json.dumps(report,indent=2))

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--resources');main(p.parse_args().resources)
