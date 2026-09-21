"""Package the final signed app, reproducible sources, report, and own-room auditions."""
from pathlib import Path
import shutil,subprocess,hashlib,plistlib,json,struct
root=Path(__file__).resolve().parents[1];r=root/'artifacts/machine-tape-001';release=root/'dist/Tide-Room-0.7.0-mac-arm64';archive=release.parent/(release.name+'.zip');assert not archive.exists()
validation=json.load(open(r/'delivery-validation.json'));assert validation['status']=='PASS'
app=release/'Tide Room.app';assert plistlib.load(open(app/'Contents/Info.plist','rb'))['CFBundleShortVersionString']=='0.7.0'
subprocess.run(['codesign','--verify','--deep','--strict',str(app)],check=True)
# Signing changes the signature size and its __LINKEDIT accounting. Verify all
# remaining Mach-O bytes, including code, data, and linker metadata.
def executable_payload_hash(path):
 b=bytearray(path.read_bytes());assert struct.unpack_from('<I',b)[0]==0xfeedfacf
 n=struct.unpack_from('<I',b,16)[0];offset=32;signature=None
 for _ in range(n):
  cmd,size=struct.unpack_from('<II',b,offset)
  if cmd==0x19 and b[offset+8:offset+24].rstrip(b'\0')==b'__LINKEDIT':
   b[offset+32:offset+40]=bytes(8);b[offset+48:offset+56]=bytes(8)
  if cmd==0x1d:
   signature=struct.unpack_from('<I',b,offset+8)[0];b[offset+8:offset+16]=bytes(8)
  offset+=size
 assert signature is not None
 return hashlib.sha256(b[:signature]).hexdigest()
payload=executable_payload_hash(app/'Contents/MacOS/Tide Room')
assert payload==executable_payload_hash(root/'build-room3/TideRoom_artefacts/Release/Standalone/Tide Room.app/Contents/MacOS/Tide Room')
shutil.copytree(root/'dist/Tide-Room-0.6.1-mac-arm64/Third-party notices',release/'Third-party notices')
shutil.copy2(root/'native/STUDIO-80.md',release/'STUDIO-80.md');shutil.copy2(r/'TAPE-STUDY.md',release/'TAPE-STUDY.md')
for filename in ['studio-summary.png','reference-analysis.png']:shutil.copy2(r/filename,release/filename)
listen=release/'listen';listen.mkdir()
for p in (r/'listen').glob('2*.wav'):shutil.copy2(p,listen/p.name)
# Public hardware recordings remain research references in the workspace, rather
# than being redistributed inside the app package. Keep useful links in report.
report=(release/'TAPE-STUDY.md').read_text();report=report.replace('[Hardware comparison 52]','[Original recordings for hardware comparison 52]').replace('[Hardware comparison 84]','[Original recordings for hardware comparison 84]');report=report.replace('(listen/Akai-52-Original-Hardware-Studio.wav)','(https://github.com/01tot10/neural-tape-modeling)').replace('(listen/Akai-84-Original-Hardware-Studio.wav)','(https://github.com/01tot10/neural-tape-modeling)');(release/'TAPE-STUDY.md').write_text(report)
source=release/'Source';source.mkdir();shutil.copytree(root/'native',source/'native',ignore=shutil.ignore_patterns('__pycache__','.DS_Store'))
for name in ['CMakeLists.txt','README.md']:shutil.copy2(root/name,source/name)
verify=release/'Verification';verify.mkdir()
for name in ['delivery-validation.json','test-exit-codes.json','calibration-final.json','alias-final.json','sm911-fit-full.json','hardware-comparison.json','reference-analysis.json','listening-manifest.json','studio-unit.log','studio-integration.log','studio-scene-fixed.log','legacy-regression.log']:
 shutil.copy2(r/name,verify/name)
(release/'README.txt').write_text('Tide Room 0.7.0 — Studio 80 tape\n\nOpen Tide Room.app. Tape controls contains Studio 80 / 15 ips, an A80-inspired option with an SM911 reference recording curve. Try Cream first, Bloom for more density, and Reference for a restrained calibrated curve. 16x is the cleanest quality setting.\n\nSTUDIO-80.md explains the controls. TAPE-STUDY.md records evidence, limits, real-hardware comparisons, and the verified Stop/idle fix. The listen folder contains fixed-gain matched versions of your room. Hardware recordings used as research references are available through the cited original repository; they are not bundled.\n\nSource and existing third-party notices are included. JUCE 8.0.9 is required to rebuild. Reverside and private preferences are not included.\n')
files={str(p.relative_to(release)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(release.rglob('*')) if p.is_file() and p!=release/'manifest.json'}
(release/'manifest.json').write_text(json.dumps(dict(product='Tide Room',version='0.7.0',architecture='arm64',executable_payload_sha256=payload,files=files),indent=2))
subprocess.run(['ditto','-c','-k','--sequesterRsrc','--keepParent',str(release),str(archive)],check=True);subprocess.run(['unzip','-tq',str(archive)],check=True)
result=dict(app=str(app),files=len(files),zip=str(archive),sha256=hashlib.sha256(archive.read_bytes()).hexdigest());(r/'package.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
